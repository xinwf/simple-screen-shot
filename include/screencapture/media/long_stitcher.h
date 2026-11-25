#pragma once
#include <cstdint>
#include <vector>

namespace sc::media {

// Rolling screenshot stitcher. The toGrayscale + findMostSimilarY
// (100px strip MSE) approach is based on CapLong.cpp from xland/ScreenCapture
// (https://github.com/xland/ScreenCapture, MIT License).
class LongStitcher {
public:
    static constexpr int kComparisonH = 100;
    static constexpr int kMaxDismiss = 8;
    static constexpr int kMaxResultH = 36000;

    // Start a new stitch with the first captured frame (BGRA, top-down, tightly packed).
    void begin(const uint8_t* frame, int width, int height);

    // Attempt to append a new frame. Returns true if content was appended.
    // Sets `reachedEnd` when scroll no longer changes content.
    bool appendFrame(const uint8_t* frame, bool& reachedEnd);

    const std::vector<uint8_t>& result() const noexcept { return imgData_; }
    int resultWidth() const noexcept { return imgW_; }
    int resultHeight() const noexcept { return resultH_; }
    bool initialized() const noexcept { return imgW_ > 0 && imgH_ > 0; }

    // Pure algorithms (exposed for testing / reuse).
    static std::vector<uint8_t> toGrayscale(const uint8_t* bgra, int width, int height, int stride);
    // outMsePerPixel (optional) receives the mean squared error per pixel of the
    // best match, so callers can reject matches that are not a real overlap.
    static int findMostSimilarY(const uint8_t* gray1, int gray1H,
                                const uint8_t* gray2, int gray2H, int width,
                                double* outMsePerPixel = nullptr);
    static bool framesDiffer(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
    static int findScrollByBottomStrip(const uint8_t* grayOld, const uint8_t* grayNew,
                                       int width, int stripH);

private:
    std::vector<uint8_t> imgData_;
    std::vector<uint8_t> img1_;
    int imgW_{0}, imgH_{0};
    int resultH_{0};
    int dismissTime_{0};
};

} // namespace sc::media
