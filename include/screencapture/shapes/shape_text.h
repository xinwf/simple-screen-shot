#pragma once
#include "screencapture/shapes/shape_base.h"
#include "screencapture/gfx/text_layout.h"
#include <memory>
#include <string>

namespace sc::shapes {

class ShapeText : public ShapeBase {
public:
    ShapeText(ShapeHost* host, const std::string& toolId);
    void paint(gfx::CanvasContext& ctx) override;
    void paintDragger(gfx::CanvasContext& ctx) override;
    void mouseDrag(float x, float y) override;
    void mouseDown(float x, float y) override;
    void mouseMove(float x, float y) override;
    void mouseWheel(float, float, int delta) override;
    void setCursor() override;
    void applyStyle() override;
    bool isValidWithoutDrag() const override { return true; }

    void startEdit();
    void finishEdit();

    bool isEditing{false};
    std::string text;
    core::RectF rect{};
    float fontSize{20.f};
    bool isBold{false}, isItalic{false};
    uint32_t colorValue{0xFF0000FF};
    std::string toolId{"text"};

private:
    void makeTextLayout();
    void setAttr();
    float borderPadding{6.f};
    float pressX{0.f}, pressY{0.f};
    std::unique_ptr<gfx::TextLayout> textLayout;
};

} // namespace sc::shapes
