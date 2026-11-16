#pragma once
#include "screencapture/shapes/shape_base.h"
#include <string>
#include <vector>

namespace sc::shapes {

class ShapeEraser : public ShapeBase {
public:
    ShapeEraser(ShapeHost* host, const std::string& toolId);
    void paint(gfx::CanvasContext& ctx) override;
    void paintDragger(gfx::CanvasContext& ctx) override;
    void mouseDrag(float x, float y) override;
    void mouseDown(float x, float y) override;
    void mouseUp(float x, float y) override;
    void mouseMove(float x, float y) override;
    void mouseWheel(float, float, int delta) override;
    void setCursor() override;
    void applyStyle() override;

    core::RectF rect{};
    float strokeWidth{28.f};

private:
    void makePath();
    void updateDraggers();
    void resetEraser();

    std::vector<core::RectF> draggers;
    std::vector<core::PointF> linePoints;
    gfx::PathBuilder path;
    bool pathValid{false};
    float pressX{0.f}, pressY{0.f};
    bool isRect{false}, isErasing{true};
    static constexpr core::ColorRgba kPlaceholder{0.f, 1.f, 0.f, 0.38f};
    std::string toolId{"eraser"};
};

} // namespace sc::shapes
