#include "screencapture/windows/cut_mask.h"

namespace sc::windows {

CutMask::CutMask(float dpi) : dpi_(dpi) { strokeWidth = 2.f * dpi; }

void CutMask::setBounds(float width, float height) {
    width_ = width;
    height_ = height;
    winRects_.clear();
}

MaskHit CutMask::hitTest(const core::RectF& r, const core::PointF& p) {
    const float tol = 6.f;
    const int col = p.x < r.left - tol ? 0 : (p.x > r.right + tol ? 2 : (p.x <= r.left + tol ? 0 : (p.x >= r.right - tol ? 2 : 1)));
    const int row = p.y < r.top - tol ? 0 : (p.y > r.bottom + tol ? 2 : (p.y <= r.top + tol ? 0 : (p.y >= r.bottom - tol ? 2 : 1)));
    if (p.x < r.left - tol || p.x > r.right + tol || p.y < r.top - tol || p.y > r.bottom + tol) {
        return MaskHit::NoHit;
    }
    static constexpr MaskHit grid[3][3] = {
        {MaskHit::TopLeft, MaskHit::Top, MaskHit::TopRight},
        {MaskHit::Left, MaskHit::Inside, MaskHit::Right},
        {MaskHit::BottomLeft, MaskHit::Bottom, MaskHit::BottomRight},
    };
    return grid[row][col];
}

bool CutMask::highlight(const core::PointF& pos, const std::vector<core::RectI>& windows,
                        const core::PointI& origin) {
    for (const auto& w : windows) {
        core::RectF r{static_cast<float>(w.left - origin.x), static_cast<float>(w.top - origin.y),
                      static_cast<float>(w.right - origin.x), static_cast<float>(w.bottom - origin.y)};
        r.left = std::max(0.f, r.left);
        r.top = std::max(0.f, r.top);
        r.right = std::min(width_, r.right);
        r.bottom = std::min(height_, r.bottom);
        if (r.isEmpty() || r.width() <= 6 || r.height() <= 6) continue;
        if (r.contains(pos.x, pos.y)) {
            if (!(r.left == maskRect.left && r.top == maskRect.top &&
                  r.right == maskRect.right && r.bottom == maskRect.bottom)) {
                maskRect = r;
                makeLayout();
                return true;
            }
            return false;
        }
    }
    // Fall back to full monitor rect if no child window matched
    core::RectF full{0.f, 0.f, width_, height_};
    if (!(full.left == maskRect.left && full.top == maskRect.top &&
          full.right == maskRect.right && full.bottom == maskRect.bottom)) {
        maskRect = full;
        makeLayout();
        return true;
    }
    return false;
}

void CutMask::startMakeRect(const core::PointF& pos) { pressPos_ = pos; }

void CutMask::makeRect(const core::PointF& pos) {
    auto [l, r] = std::minmax(pressPos_.x, pos.x);
    auto [t, b] = std::minmax(pressPos_.y, pos.y);
    maskRect = {std::clamp(l, 0.f, width_), std::clamp(t, 0.f, height_),
                std::clamp(r, 0.f, width_), std::clamp(b, 0.f, height_)};
    makeLayout();
}

void CutMask::startAdjust(const core::PointF& pos) {
    adjustHit_ = hasRect() ? hitTest(maskRect, pos) : MaskHit::NoHit;
    adjustStartRect_ = maskRect;
    adjustPressPos_ = pos;
    if (adjustHit_ != MaskHit::NoHit && adjustHit_ != MaskHit::Inside) adjust(pos);
}

void CutMask::adjust(const core::PointF& pos) {
    if (adjustHit_ == MaskHit::NoHit) return;
    core::RectF r = adjustStartRect_;
    if (adjustHit_ == MaskHit::Inside) {
        float dx = pos.x - adjustPressPos_.x, dy = pos.y - adjustPressPos_.y;
        float rw = r.width(), rh = r.height();
        r.left = std::clamp(r.left + dx, 0.f, std::max(0.f, width_ - rw));
        r.top = std::clamp(r.top + dy, 0.f, std::max(0.f, height_ - rh));
        r.right = r.left + rw;
        r.bottom = r.top + rh;
    } else {
        float px = std::clamp(pos.x, 0.f, width_);
        float py = std::clamp(pos.y, 0.f, height_);
        switch (adjustHit_) {
            case MaskHit::Left: r.left = px; break;
            case MaskHit::Right: r.right = px; break;
            case MaskHit::Top: r.top = py; break;
            case MaskHit::Bottom: r.bottom = py; break;
            case MaskHit::TopLeft: r.left = px; r.top = py; break;
            case MaskHit::TopRight: r.right = px; r.top = py; break;
            case MaskHit::BottomRight: r.right = px; r.bottom = py; break;
            case MaskHit::BottomLeft: r.left = px; r.bottom = py; break;
            default: break;
        }
        if (r.right < r.left) std::swap(r.left, r.right);
        if (r.bottom < r.top) std::swap(r.top, r.bottom);
        if (r.width() < kMinSize) {
            if (adjustHit_ == MaskHit::Left || adjustHit_ == MaskHit::TopLeft || adjustHit_ == MaskHit::BottomLeft)
                r.left = r.right - kMinSize;
            else
                r.right = r.left + kMinSize;
        }
        if (r.height() < kMinSize) {
            if (adjustHit_ == MaskHit::Top || adjustHit_ == MaskHit::TopLeft || adjustHit_ == MaskHit::TopRight)
                r.top = r.bottom - kMinSize;
            else
                r.bottom = r.top + kMinSize;
        }
    }
    if (r.left != maskRect.left || r.top != maskRect.top ||
        r.right != maskRect.right || r.bottom != maskRect.bottom) {
        maskRect = r;
        makeLayout();
    }
}

void CutMask::makeLayout() {
    if (!hasRect()) { layout_.reset(); return; }
    std::string text = "X:" + std::to_string(static_cast<int>(maskRect.left)) +
        " Y:" + std::to_string(static_cast<int>(maskRect.top)) +
        " W:" + std::to_string(static_cast<int>(maskRect.width())) +
        " H:" + std::to_string(static_cast<int>(maskRect.height()));
    layout_ = std::make_unique<gfx::TextLayout>(text, 10.f * dpi_);
    auto m = layout_->metrics();
    labelRect_ = {maskRect.left, maskRect.top - m.height - 6.f * dpi_,
                  maskRect.left + m.width + 8.f * dpi_, maskRect.top - 2.f * dpi_};
    if (labelRect_.top < 0) {
        labelRect_.top = maskRect.top + 2.f * dpi_;
        labelRect_.bottom = maskRect.top + m.height + 8.f * dpi_;
    }
}

void CutMask::paint(gfx::CanvasContext& ctx) {
    if (!hasRect()) return;
    // Original xland/ScreenCapture CutMask uses 46% black (0x00000075), NOT 80% black.
    auto dim = core::ColorRgba::fromRgbaHex(0x00000075);
    if (maskRect.top > 0) ctx.fillRect({0, 0, width_, maskRect.top}, dim);
    if (maskRect.bottom < height_) ctx.fillRect({0, maskRect.bottom, width_, height_}, dim);
    if (maskRect.left > 0) ctx.fillRect({0, maskRect.top, maskRect.left, maskRect.bottom}, dim);
    if (maskRect.right < width_) ctx.fillRect({maskRect.right, maskRect.top, width_, maskRect.bottom}, dim);

    ctx.strokeRect(maskRect, core::ColorRgba::fromHex(0x1677FF), strokeWidth);

    if (showHandles) {
        const float hs = 8.f * dpi_;
        const float half = hs / 2.f;
        const float l = maskRect.left, t = maskRect.top;
        const float r = maskRect.right, b = maskRect.bottom;
        const float mx = (l + r) * 0.5f, my = (t + b) * 0.5f;
        const core::PointF pts[8] = {
            {l, t}, {mx, t}, {r, t},
            {r, my}, {r, b}, {mx, b},
            {l, b}, {l, my},
        };
        auto white = core::ColorRgba::fromHex(0xFFFFFF);
        auto blue = core::ColorRgba::fromHex(0x1677FF);
        for (const auto& p : pts) {
            core::RectF h{p.x - half, p.y - half, p.x + half, p.y + half};
            ctx.fillRect(h, white);
            ctx.strokeRect(h, blue, dpi_);
        }
    }
    if (hideLabel || !layout_) return;
    ctx.fillRect(labelRect_, core::ColorRgba::fromHex(0x1677FF));
    ctx.drawText(labelRect_.left + 4.f * dpi_, labelRect_.top, *layout_,
                 core::ColorRgba::fromHex(0xFFFFFF));
}

} // namespace sc::windows
