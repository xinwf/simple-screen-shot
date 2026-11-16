#pragma once
#include "screencapture/shapes/shape_base.h"
#include <string>
#include <vector>

namespace sc::shapes {

class ShapeEllipse : public ShapeBase {
public:
    ShapeEllipse(ShapeHost* host, const std::string& toolId);
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
    core::ColorRgba color{1.f, 0.f, 0.f, 1.f};
    float strokeWidth{2.f};
    bool isFill{false};
    std::string toolId{"ellipse"};

private:
    void updateDraggers();
    void syncEllipse();
    std::vector<core::RectF> draggers;
    float pressX{0.f}, pressY{0.f};
    float cx{0.f}, cy{0.f}, rx{0.f}, ry{0.f};
};

} // namespace sc::shapes
