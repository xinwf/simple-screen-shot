#pragma once
#include "screencapture/core/types.h"
#include "screencapture/gfx/canvas_context.h"
#include "screencapture/gfx/text_layout.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace sc::windows {

enum class MaskHit {
    NoHit, Inside, Left, Top, Right, Bottom,
    TopLeft, TopRight, BottomRight, BottomLeft,
};

// Nine-region selection mask with window auto-snap and size label.
class CutMask {
public:
    explicit CutMask(float dpi);

    void setBounds(float width, float height);
    // windows are in desktop coords; origin is this overlay's desktop offset.
    bool highlight(const core::PointF& pos, const std::vector<core::RectI>& windows,
                   const core::PointI& origin);
    void startMakeRect(const core::PointF& pos);
    void makeRect(const core::PointF& pos);
    void startAdjust(const core::PointF& pos);
    void adjust(const core::PointF& pos);
    bool hasRect() const noexcept { return maskRect.right > maskRect.left && maskRect.bottom > maskRect.top; }
    MaskHit hitTestAt(const core::PointF& pos) const { return hasRect() ? hitTest(maskRect, pos) : MaskHit::NoHit; }

    void paint(gfx::CanvasContext& ctx);

    core::RectF maskRect{};
    float strokeWidth{2.f};
    bool hideLabel{false};
    bool showHandles{false}; // Adjust stage: 4 corners + 4 edge midpoints

private:
    static MaskHit hitTest(const core::RectF& rect, const core::PointF& p);
    void makeLayout();
    float dpi_{1.f};
    float width_{0.f}, height_{0.f};
    core::PointF pressPos_{};
    MaskHit adjustHit_{MaskHit::NoHit};
    core::RectF adjustStartRect_{};
    core::PointF adjustPressPos_{};
    std::vector<core::RectF> winRects_;
    std::unique_ptr<gfx::TextLayout> layout_;
    core::RectF labelRect_{};
    static constexpr float kMinSize{4.f};
};

} // namespace sc::windows
