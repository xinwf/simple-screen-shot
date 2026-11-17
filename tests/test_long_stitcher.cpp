// Unit tests for LongStitcher, the pure scroll-stitching core. No display,
// no GTK: frames are synthetic BGRA buffers exactly like ScreenCapturer
// produces (alpha always 255, tightly packed rows).
#include "screencapture/media/long_stitcher.h"

#include <cassert>
#include <cstdio>
#include <vector>

using sc::media::LongStitcher;

namespace {

// Synthetic scrolling page: every absolute *pixel* gets a deterministic,
// well-scrambled color (splitmix64-style hash), so a window
// [topRow, topRow+h) models a page scrolled by topRow. Row-constant colors
// would let a 1-row overlap match by luck (uniform value collision), so
// real per-pixel variation is required to exercise the mismatch guards.
uint32_t pixelHash(int row, int x) {
    uint64_t v = (static_cast<uint64_t>(row) << 24) | static_cast<uint64_t>(x);
    uint64_t t = v + 0x9E3779B97F4A7C15ull;
    t = (t ^ (t >> 30)) * 0xBF58476D1CE4E5B9ull;
    t = (t ^ (t >> 27)) * 0x94D049BB133111EBull;
    return static_cast<uint32_t>(t ^ (t >> 31));
}

std::vector<uint8_t> makeFrame(int w, int h, int topRow) {
    std::vector<uint8_t> f(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t hv = pixelHash(topRow + y, x);
            uint8_t* p = &f[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = static_cast<uint8_t>(hv & 0xFF);          // B
            p[1] = static_cast<uint8_t>((hv >> 8) & 0xFF);   // G
            p[2] = static_cast<uint8_t>((hv >> 16) & 0xFF);  // R
            p[3] = 255;                                      // forced alpha
        }
    }
    return f;
}

// 1) toGrayscale uses the BGRA luma weights (0-255, truncated).
void testGrayscale() {
    uint8_t px[16] = {0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 0, 0, 255, 255};
    auto g = LongStitcher::toGrayscale(px, 4, 1, 16);
    assert(g[0] == 0);    // black
    assert(g[1] == 255);  // white
    assert(g[2] == 29);   // B=255 -> 114 weight
    assert(g[3] == 76);   // R=255 -> 299 weight
}

// 2) Identical frames never append; after kMaxDismiss of them the session
//    is declared finished (mouse stopped scrolling).
void testIdenticalFramesEndSession() {
    LongStitcher st;
    const int w = 64, h = 100;
    auto f0 = makeFrame(w, h, 0);
    st.begin(f0.data(), w, h);
    bool end = false;
    for (int i = 0; i < LongStitcher::kMaxDismiss; ++i) {
        assert(!st.appendFrame(f0.data(), end));
        assert(!end);
    }
    assert(!st.appendFrame(f0.data(), end));
    assert(end);
    assert(st.resultHeight() == h);
}

// 3) A real scroll of 10 rows appends exactly the 10 revealed rows.
void testScrollAppendsRevealedRows() {
    LongStitcher st;
    const int w = 64, h = 100;
    auto f0 = makeFrame(w, h, 0);
    st.begin(f0.data(), w, h);
    bool end = false;
    auto f1 = makeFrame(w, h, 10);
    assert(st.appendFrame(f1.data(), end));
    assert(!end);
    assert(st.resultHeight() == h + 10);
    // The band painted at the tail must be the new content, not garbage:
    // row 109 of the result equals row 99 of frame f1.
    const uint8_t* res = st.result().data();
    const uint8_t* tail = &f1[static_cast<size_t>(h - 1) * w * 4];
    const uint8_t* got = res + static_cast<size_t>(109) * w * 4;
    for (int x = 0; x < w; ++x) {
        assert(got[x * 4 + 0] == tail[x * 4 + 0]);
        assert(got[x * 4 + 2] == tail[x * 4 + 2]);
    }
}

// 4) An unrelated frame (page jump, not a scroll) is rejected by the match
//    guards instead of painting a misaligned ghost band.
void testUnrelatedFrameRejected() {
    LongStitcher st;
    const int w = 64, h = 100;
    auto f0 = makeFrame(w, h, 0);
    st.begin(f0.data(), w, h);
    bool end = false;
    auto jump = makeFrame(w, h, 7000);
    st.appendFrame(jump.data(), end);
    assert(st.resultHeight() == h);
}

} // namespace

int main() {
    testGrayscale();
    testIdenticalFramesEndSession();
    testScrollAppendsRevealedRows();
    testUnrelatedFrameRejected();
    std::printf("long_stitcher: all tests passed\n");
    return 0;
}
