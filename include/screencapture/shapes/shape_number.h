#pragma once
#include "screencapture/shapes/shape_base.h"
#include "screencapture/gfx/text_layout.h"
#include <memory>
#include <string>
#include <vector>

namespace sc::shapes {

class ShapeNumber : public ShapeBase {
public:
    ShapeNumber(ShapeHost* host, const std::string& toolId);
    void paint(gfx::CanvasContext& ctx) override;
    void paintDragger(gfx::CanvasContext& ctx) override;
    void mouseDrag(float x, float y) override;
    void mouseDown(float x, float y) override;
    void mouseUp(float x, float y) override;
    void mouseMove(float x, float y) override;
    void mouseWheel(float, float, int delta) override;
    void setCursor() override;
    void applyStyle() override;
    bool isValidWithoutDrag() const override { return true; }

    int val{1};

private:
    void makePath();
    void updateDraggers();
    core::PointF localPoint(float degrees);
    core::PointF transformPoint(const core::PointF& point);

    std::vector<core::RectF> draggers;
    gfx::PathBuilder path;
    bool pathValid{false};
    std::unique_ptr<gfx::TextLayout> textLayout;
    core::ColorRgba color{1.f, 0.f, 0.f, 1.f};
    static constexpr core::ColorRgba kTextColor{1.f, 1.f, 1.f, 1.f};
    float pressX{0.f}, pressY{0.f};
    float cx{0.f}, cy{0.f}, r{16.f}, angle{270.f};
    core::PointF tip{0.f, 0.f}, mid{0.f, 0.f};
    bool isFill{true}, isWheel{false};
    std::string toolId{"number"};
};

} // namespace sc::shapes
