#pragma once
#include "screencapture/shapes/shape_base.h"
#include <string>
#include <vector>

namespace sc::shapes {

class ShapeLine : public ShapeBase {
public:
    ShapeLine(ShapeHost* host, const std::string& toolId);
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
    float strokeWidth{12.f};
    std::string toolId{"line"};

private:
    void makePath();
    static float pointToSegmentDistance(const core::PointF& p, const core::PointF& a,
                                        const core::PointF& b);
    void hitTest(const core::PointF& mousePos);
    void updateDraggers();

    std::vector<core::RectF> draggers;
    std::vector<core::PointF> linePoints;
    gfx::PathBuilder path;
    float pressX{0.f}, pressY{0.f};
    bool pathValid{false};
};

} // namespace sc::shapes
