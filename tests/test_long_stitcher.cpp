// Unit tests for LongStitcher, the pure scroll-stitching core. No display,
// no GTK: frames are synthetic BGRA buffers exactly like ScreenCapturer
// produces (alpha always 255, tightly packed rows).
#include "screencapture/media/long_stitcher.h"

#include <cassert>
#include <cstdio>
#include <cstring>
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

std::vector<uint8_t> makeFrame(int w, int h, int topRow, int fixedRows = 0) {
    std::vector<uint8_t> f(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int pageRow = y < fixedRows ? y : topRow + y;
            const uint32_t hv = pixelHash(pageRow, x);
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

// 5) The bottom-strip fallback returns the forward scroll distance.
void testBottomStripReturnsScrollDistance() {
    const int w = 64, h = 100;
    auto f0 = makeFrame(w, h, 0);
    auto f1 = makeFrame(w, h, 10);
    auto g0 = LongStitcher::toGrayscale(f0.data(), w, h, w * 4);
    auto g1 = LongStitcher::toGrayscale(f1.data(), w, h, w * 4);
    assert(LongStitcher::findScrollByBottomStrip(g0.data(), g1.data(), w, h) == 10);

    // A single coincidentally equal row is not enough evidence of an overlap.
    auto jump = makeFrame(w, h, 7000);
    auto gj = LongStitcher::toGrayscale(jump.data(), w, h, w * 4);
    std::memcpy(gj.data(), g0.data() + static_cast<size_t>(h - 1) * w, w);
    assert(LongStitcher::findScrollByBottomStrip(g0.data(), gj.data(), w, h) == 0);
}

// 6) Stitching appends only newly revealed rows. Repainting the overlap with a
//    newer frame mixes two moments of animated/changing content into one band.
void testOverlapIsNotRepainted() {
    LongStitcher st;
    const int w = 64, h = 300;
    auto f0 = makeFrame(w, h, 0);
    auto f1 = makeFrame(w, h, 10);
    uint8_t* changed = &f1[static_cast<size_t>(190) * w * 4];
    changed[0] ^= 0xFF;
    changed[1] ^= 0xFF;
    changed[2] ^= 0xFF;

    st.begin(f0.data(), w, h);
    bool end = false;
    assert(st.appendFrame(f1.data(), end));
    assert(!end);
    assert(st.resultHeight() == h + 10);
    const uint8_t* res = st.result().data();
    assert(std::memcmp(res + static_cast<size_t>(200) * w * 4,
                       f0.data() + static_cast<size_t>(200) * w * 4, w * 4) == 0);
    assert(std::memcmp(res + static_cast<size_t>(309) * w * 4,
                       f1.data() + static_cast<size_t>(299) * w * 4, w * 4) == 0);
}

// 7) A fixed header followed by a scrolling body still appends the exact shift.
void testFixedHeaderScroll() {
    LongStitcher st;
    const int w = 64, h = 300, header = 50;
    auto f0 = makeFrame(w, h, 0, header);
    auto f1 = makeFrame(w, h, 10, header);
    st.begin(f0.data(), w, h);
    bool end = false;
    assert(st.appendFrame(f1.data(), end));
    assert(!end);
    assert(st.resultHeight() == h + 10);
    assert(std::memcmp(st.result().data() + static_cast<size_t>(309) * w * 4,
                       f1.data() + static_cast<size_t>(299) * w * 4, w * 4) == 0);
}

// 8) Multiple different scroll increments reproduce the page exactly, with
//    no duplicated or missing rows at the accumulated seams.
void testMultipleScrollsMatchPage() {
    LongStitcher st;
    const int w = 64, h = 180;
    const int shifts[] = {7, 23, 61};
    auto frame = makeFrame(w, h, 0);
    st.begin(frame.data(), w, h);
    int top = 0;
    bool end = false;
    for (int shift : shifts) {
        top += shift;
        frame = makeFrame(w, h, top);
        assert(st.appendFrame(frame.data(), end));
        assert(!end);
    }
    auto expected = makeFrame(w, h + top, 0);
    assert(st.resultHeight() == h + top);
    assert(st.result() == expected);
}

// 9) A false match in the top probe must be rejected by the seam anchor.
void testFalseTopMatchRejected() {
    LongStitcher st;
    const int w = 64, h = 300;
    auto f0 = makeFrame(w, h, 0);
    auto f1 = makeFrame(w, h, 60);
    // The first 100 rows now look like a 10-row scroll, while the rest of the
    // frame actually moved 60 rows. Appending here would duplicate a band.
    std::memcpy(f1.data(), f0.data() + static_cast<size_t>(10) * w * 4,
                static_cast<size_t>(100) * w * 4);
    st.begin(f0.data(), w, h);
    bool end = false;
    assert(!st.appendFrame(f1.data(), end));
    assert(!end);
    assert(st.resultHeight() == h);
}

// 10) Unmatched moving content is not proof that the page reached its end.
//    Only repeated observations of the accepted frame may finish the session.
void testRejectedFramesDoNotEndSession() {
    LongStitcher st;
    const int w = 64, h = 300;
    auto f0 = makeFrame(w, h, 0);
    st.begin(f0.data(), w, h);
    bool end = false;
    for (int i = 1; i <= LongStitcher::kMaxDismiss + 3; ++i) {
        auto jump = makeFrame(w, h, 100000 + i * 1000);
        assert(!st.appendFrame(jump.data(), end));
        assert(!end);
        assert(st.resultHeight() == h);
    }
}

} // namespace

int main() {
    testGrayscale();
    testIdenticalFramesEndSession();
    testScrollAppendsRevealedRows();
    testUnrelatedFrameRejected();
    testBottomStripReturnsScrollDistance();
    testOverlapIsNotRepainted();
    testFixedHeaderScroll();
    testMultipleScrollsMatchPage();
    testFalseTopMatchRejected();
    testRejectedFramesDoNotEndSession();
    std::printf("long_stitcher: all tests passed\n");
    return 0;
}
