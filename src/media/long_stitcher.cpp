#include "screencapture/media/long_stitcher.h"
#include <algorithm>
#include <cfloat>
#include <cstring>

namespace sc::media {

namespace {
constexpr double kBottomMatchMinRatio = 0.9;
constexpr double kBottomMatchMaxError = 40000;
constexpr int kMaxSettleRecheck = 2;
// Mean squared error per pixel (0-255 gray) above which the best offset is
// treated as "no real overlap". Genuine overlaps of two settled frames of the
// same page are near zero; a wrong offset on text content is far above this.
// Rejecting such a frame avoids painting a misaligned band over the result,
// which is what shows up as duplicated ("ghost") rows in the long screenshot.
constexpr double kMaxMatchMse = 30.0;
}

std::vector<uint8_t> LongStitcher::toGrayscale(const uint8_t* bgra, int width, int height, int stride) {
    std::vector<uint8_t> gray(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = bgra + static_cast<size_t>(y) * stride;
        uint8_t* dst = gray.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            dst[x] = static_cast<uint8_t>((src[x*4] * 114 + src[x*4+1] * 587 + src[x*4+2] * 299) / 1000);
        }
    }
    return gray;
}

int LongStitcher::findMostSimilarY(const uint8_t* gray1, int gray1H,
                                   const uint8_t* gray2, int gray2H, int width,
                                   double* outMsePerPixel) {
    if (outMsePerPixel) *outMsePerPixel = DBL_MAX;
    int searchH = gray1H - gray2H + 1;
    if (searchH <= 0) return 0;
    double minAvgError = DBL_MAX;
    int bestY = 0;
    for (int y = 0; y < searchH; ++y) {
        double error = 0.0;
        const double maxAllowedError = minAvgError * gray2H;
        for (int row = 0; row < gray2H; ++row) {
            const uint8_t* row1 = gray1 + static_cast<size_t>(y + row) * width;
            const uint8_t* row2 = gray2 + static_cast<size_t>(row) * width;
            for (int x = 0; x < width; ++x) {
                int diff = static_cast<int>(row1[x]) - static_cast<int>(row2[x]);
                error += diff * diff;
            }
            if (error >= maxAllowedError) break;
        }
        double avgError = error / gray2H;
        if (avgError < minAvgError) {
            minAvgError = avgError;
            bestY = y;
        }
    }
    if (outMsePerPixel) *outMsePerPixel = minAvgError / width;
    return bestY;
}

bool LongStitcher::framesDiffer(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return true;
    return std::memcmp(a.data(), b.data(), a.size()) != 0;
}

int LongStitcher::findScrollByBottomStrip(const uint8_t* grayOld, const uint8_t* grayNew,
                                          int width, int stripH) {
    double minAvgError = DBL_MAX, avgAtZero = DBL_MAX;
    int bestS = 0;
    for (int s = 0; s < stripH; ++s) {
        int rows = stripH - s;
        double error = 0.0;
        const double maxAllowedError = minAvgError * rows;
        for (int r = 0; r < rows; ++r) {
            const uint8_t* row1 = grayOld + static_cast<size_t>(s + r) * width;
            const uint8_t* row2 = grayNew + static_cast<size_t>(r) * width;
            for (int x = 0; x < width; ++x) {
                int diff = static_cast<int>(row1[x]) - static_cast<int>(row2[x]);
                error += diff * diff;
            }
            if (error >= maxAllowedError) break;
        }
        double avgError = error / rows;
        if (s == 0) avgAtZero = avgError;
        if (avgError < minAvgError) { minAvgError = avgError; bestS = s; }
    }
    if (bestS <= 0) return 0;
    if (minAvgError >= avgAtZero * kBottomMatchMinRatio) return 0;
    if (minAvgError > kBottomMatchMaxError) return 0;
    return bestS;
}

void LongStitcher::begin(const uint8_t* frame, int width, int height) {
    imgW_ = width; imgH_ = height;
    resultH_ = height;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    imgData_.reserve(bytes * 4);
    imgData_.assign(frame, frame + bytes);
    img1_ = imgData_;
    changeStartY_ = -1;
    firstCheck_ = true;
    dismissTime_ = 0;
    settleRecheckCount_ = 0;
}

bool LongStitcher::appendFrame(const uint8_t* data, bool& reachedEnd) {
    reachedEnd = false;
    const size_t frameBytes = static_cast<size_t>(imgW_) * imgH_ * 4;
    std::vector<uint8_t> frame(data, data + frameBytes);

    if (firstCheck_) {
        changeStartY_ = -1;
        // First row whose pixels changed vs the previous frame. Whole-row
        // memcmp replaces the per-pixel compare (captured alpha is always
        // 255, so comparing the A byte too cannot add false positives).
        const int rowBytes = imgW_ * 4;
        for (int y = 0; y < imgH_; ++y) {
            if (std::memcmp(img1_.data() + static_cast<size_t>(y) * rowBytes,
                            frame.data() + static_cast<size_t>(y) * rowBytes,
                            rowBytes) != 0) {
                changeStartY_ = y;
                break;
            }
        }
        if (changeStartY_ == -1) {
            if (++dismissTime_ > kMaxDismiss) { reachedEnd = true; return false; }
            return false;
        }
        firstCheck_ = false;
    }

    int rowPix = imgW_ * 4;
    int stripH = std::min(kComparisonH, imgH_ - changeStartY_);
    if (stripH <= 0) return false;
    int img1StripH = imgH_ - changeStartY_;
    auto gray1 = toGrayscale(img1_.data() + static_cast<size_t>(changeStartY_) * rowPix,
                             imgW_, img1StripH, rowPix);
    // Full-height grayscale of the frame below changeStartY_.
    auto gray2 = toGrayscale(frame.data() + static_cast<size_t>(changeStartY_) * rowPix,
                             imgW_, stripH, rowPix);
    double matchMse = 0.0;
    int y = findMostSimilarY(gray1.data(), img1StripH, gray2.data(), stripH, imgW_,
                             &matchMse);
    // A poor best match means the frame was not captured at a trustworthy
    // scroll offset; treat it like "no change yet" and retry with the next
    // frame instead of appending a misaligned band.
    const bool untrustedMatch = (y > 0 && matchMse > kMaxMatchMse);
    if (untrustedMatch) y = 0;
    if (y == 0 && !untrustedMatch) {
        auto gray1Bottom = toGrayscale(img1_.data() + static_cast<size_t>(imgH_ - stripH) * rowPix,
                                       imgW_, stripH, rowPix);
        auto gray2Bottom = toGrayscale(frame.data() + static_cast<size_t>(imgH_ - stripH) * rowPix,
                                       imgW_, stripH, rowPix);
        y = findScrollByBottomStrip(gray1Bottom.data(), gray2Bottom.data(), imgW_, stripH);
    }
    if (y == 0) {
        if (framesDiffer(frame, img1_)) {
            if (settleRecheckCount_ < kMaxSettleRecheck) {
                ++settleRecheckCount_;
                return false;
            }
        }
        settleRecheckCount_ = 0;
        if (++dismissTime_ > kMaxDismiss) { reachedEnd = true; return false; }
        return false;
    }
    dismissTime_ = 0;
    settleRecheckCount_ = 0;

    int paintStart = resultH_ - (imgH_ - y - changeStartY_);
    int newResultH = paintStart + (imgH_ - changeStartY_);
    const size_t newSizeBytes = static_cast<size_t>(rowPix) * newResultH;
    if (imgData_.size() < newSizeBytes) {
        if (imgData_.capacity() < newSizeBytes) {
            imgData_.reserve(std::max(newSizeBytes, imgData_.capacity() * 3 / 2));
        }
        imgData_.resize(newSizeBytes);
    }
    for (int row = 0; row < imgH_ - changeStartY_; ++row) {
        std::memcpy(imgData_.data() + static_cast<size_t>(paintStart + row) * rowPix,
                    frame.data() + static_cast<size_t>(changeStartY_ + row) * rowPix,
                    static_cast<size_t>(rowPix));
    }
    img1_ = std::move(frame);
    resultH_ = newResultH;
    if (resultH_ > kMaxResultH) { reachedEnd = true; return true; }
    return true;
}

} // namespace sc::media
