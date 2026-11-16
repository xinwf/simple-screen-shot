#pragma once
#include "screencapture/shapes/shape_base.h"
#include <string>
#include <vector>

namespace sc::shapes {

class ShapeArrow : public ShapeBase {
public:
    ShapeArrow(ShapeHost* host, const std::string& toolId);
    void paint(gfx::CanvasContext& ctx) override;
    void paintDragger(gfx::CanvasContext& ctx) override;
    void mouseDrag(float x, float y) override;
    void mouseDown(float x, float y) override;
    void mouseUp(float x, float y) override;
    void mouseMove(float x, float y) override;
    void mouseWheel(float, float, int delta) override;
    void setCursor() override;
    void applyStyle() override;

    core::ColorRgba color{1.f, 0.f, 0.f, 1.f};
    float arrowSize{12.f};
    bool isFill{true};

private:
    void makeArrow();
    void updateDraggers();
    static void constrainToEightDirections(float anchorX, float anchorY, float mouseX, float mouseY,
                                           float& targetX, float& targetY);

    std::vector<core::RectF> draggers;
    gfx::PathBuilder path;
    bool pathValid{false};
    float pressX{0.f}, pressY{0.f};
    float startX{0.f}, startY{0.f}, endX{0.f}, endY{0.f};
    std::string toolId{"arrow"};
};

} // namespace sc::shapes
