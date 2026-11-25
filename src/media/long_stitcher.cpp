#include "screencapture/media/long_stitcher.h"
#include <algorithm>
#include <cfloat>
#include <cstring>

namespace sc::media {

namespace {
constexpr double kBottomMatchMinRatio = 0.9;
constexpr int kMinBottomOverlap = 24;
// Mean squared error per pixel (0-255 gray) above which the best offset is
// treated as "no real overlap". Genuine overlaps of two settled frames of the
// same page are near zero; a wrong offset on text content is far above this.
// Rejecting such a frame avoids painting a misaligned band over the result,
// which is what shows up as duplicated ("ghost") rows in the long screenshot.
constexpr double kMaxMatchMse = 30.0;

double meanSquaredError(const uint8_t* a, const uint8_t* b, int rows, int width) {
    double error = 0.0;
    for (int row = 0; row < rows; ++row) {
        const uint8_t* ar = a + static_cast<size_t>(row) * width;
        const uint8_t* br = b + static_cast<size_t>(row) * width;
        for (int x = 0; x < width; ++x) {
            int diff = static_cast<int>(ar[x]) - static_cast<int>(br[x]);
            error += diff * diff;
        }
    }
    return error / (static_cast<double>(rows) * width);
}
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
    if (!grayOld || !grayNew || width <= 0 || stripH < kMinBottomOverlap) return 0;
    double minAvgError = DBL_MAX, avgAtZero = DBL_MAX;
    int bestS = 0;
    // Very short overlaps can match a blank row or a fragment of text by
    // chance. Require enough rows to distinguish a real repeated region.
    for (int s = 0; s <= stripH - kMinBottomOverlap; ++s) {
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
    if (minAvgError / width > kMaxMatchMse) return 0;
    return bestS;
}

void LongStitcher::begin(const uint8_t* frame, int width, int height) {
    imgW_ = width; imgH_ = height;
    resultH_ = height;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    imgData_.reserve(bytes * 4);
    imgData_.assign(frame, frame + bytes);
    img1_ = imgData_;
    dismissTime_ = 0;
}

bool LongStitcher::appendFrame(const uint8_t* data, bool& reachedEnd) {
    reachedEnd = false;
    const size_t frameBytes = static_cast<size_t>(imgW_) * imgH_ * 4;
    std::vector<uint8_t> frame(data, data + frameBytes);

    // Recompute the unchanged prefix for every frame. It may be a fixed
    // header, but it may also change after a resize, expansion or animation.
    int changeStartY = -1;
    const int rowBytes = imgW_ * 4;
    for (int y = 0; y < imgH_; ++y) {
        if (std::memcmp(img1_.data() + static_cast<size_t>(y) * rowBytes,
                        frame.data() + static_cast<size_t>(y) * rowBytes,
                        rowBytes) != 0) {
            changeStartY = y;
            break;
        }
    }
    if (changeStartY == -1) {
        if (++dismissTime_ > kMaxDismiss) { reachedEnd = true; return false; }
        return false;
    }
    // A different but unmatched frame is not evidence that scrolling ended.
    dismissTime_ = 0;

    int rowPix = imgW_ * 4;
    int stripH = std::min(kComparisonH, imgH_ - changeStartY);
    if (stripH <= 0) return false;
    int img1StripH = imgH_ - changeStartY;
    auto gray1 = toGrayscale(img1_.data() + static_cast<size_t>(changeStartY) * rowPix,
                             imgW_, img1StripH, rowPix);
    // Full-height grayscale of the frame below the unchanged prefix.
    auto gray2 = toGrayscale(frame.data() + static_cast<size_t>(changeStartY) * rowPix,
                             imgW_, stripH, rowPix);
    double matchMse = 0.0;
    int y = findMostSimilarY(gray1.data(), img1StripH, gray2.data(), stripH, imgW_,
                             &matchMse);
    // A poor best match means the frame was not captured at a trustworthy
    // scroll offset; treat it like "no change yet" and retry with the next
    // frame instead of appending a misaligned band.
    const bool untrustedMatch = (y <= 0 || matchMse > kMaxMatchMse);
    if (untrustedMatch) {
        auto gray1Bottom = toGrayscale(img1_.data() + static_cast<size_t>(imgH_ - stripH) * rowPix,
                                       imgW_, stripH, rowPix);
        auto gray2Bottom = toGrayscale(frame.data() + static_cast<size_t>(imgH_ - stripH) * rowPix,
                                       imgW_, stripH, rowPix);
        y = findScrollByBottomStrip(gray1Bottom.data(), gray2Bottom.data(), imgW_, stripH);
    }
    if (y <= 0 || y >= imgH_ - changeStartY) return false;

    // Validate a second anchor at the bottom of the overlap as well. A single
    // strip can coincide on blank areas or repeated rows even at a wrong
    // offset; the seam anchor rejects that before any pixels are appended.
    int validationH = std::min(kComparisonH, imgH_ - changeStartY - y);
    if (validationH <= 0) return false;
    auto oldAnchor = toGrayscale(img1_.data() + static_cast<size_t>(imgH_ - validationH) * rowPix,
                                 imgW_, validationH, rowPix);
    auto newAnchor = toGrayscale(frame.data() + static_cast<size_t>(imgH_ - y - validationH) * rowPix,
                                 imgW_, validationH, rowPix);
    if (meanSquaredError(oldAnchor.data(), newAnchor.data(), validationH, imgW_) > kMaxMatchMse) {
        return false;
    }

    // Keep the already captured overlap untouched. Repainting it with a newer
    // frame mixes two moments of animated content and creates ghost bands.
    int newResultH = resultH_ + y;
    const size_t newSizeBytes = static_cast<size_t>(rowPix) * newResultH;
    if (imgData_.size() < newSizeBytes) {
        if (imgData_.capacity() < newSizeBytes) {
            imgData_.reserve(std::max(newSizeBytes, imgData_.capacity() * 3 / 2));
        }
        imgData_.resize(newSizeBytes);
    }
    std::memcpy(imgData_.data() + static_cast<size_t>(resultH_) * rowPix,
                frame.data() + static_cast<size_t>(imgH_ - y) * rowPix,
                static_cast<size_t>(y) * rowPix);
    img1_ = std::move(frame);
    resultH_ = newResultH;
    if (resultH_ > kMaxResultH) { reachedEnd = true; return true; }
    return true;
}

} // namespace sc::media
