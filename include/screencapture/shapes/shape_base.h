#pragma once
#include "screencapture/shapes/shape_host.h"
#include "screencapture/gfx/canvas_context.h"

namespace sc::shapes {

class ShapeBase {
public:
    explicit ShapeBase(ShapeHost* host) : host(host), draggerSize(6.f * host->hostDpi()) {}
    virtual ~ShapeBase() = default;

    virtual void paint(gfx::CanvasContext& ctx) = 0;
    virtual void paintDragger(gfx::CanvasContext&) {}
    virtual void mouseMove(float /*x*/, float /*y*/) {}
    virtual void mouseDrag(float /*x*/, float /*y*/) {}
    virtual void mouseDown(float /*x*/, float /*y*/) {}
    virtual void mouseUp(float /*x*/, float /*y*/) {}
    virtual void mouseWheel(float /*x*/, float /*y*/, int /*delta*/) {}
    virtual void setCursor() {}
    virtual void applyStyle() {}
    virtual bool isValidWithoutDrag() const { return false; }

    static bool isInRect(const core::RectF& rect, float x, float y) {
        return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
    }

    ShapeHost* host{nullptr};
    bool isUndo{false};
    int hoverDraggerIndex{-1};
    float draggerSize{6.f};

protected:
    core::ColorRgba draggerColor{0.f, 0.f, 0.f, 1.f};
};

} // namespace sc::shapes
