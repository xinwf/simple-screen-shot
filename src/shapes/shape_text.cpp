#include "screencapture/shapes/shape_text.h"
#include "screencapture/shapes/shape_history.h"
#include <algorithm>

namespace sc::shapes {

ShapeText::ShapeText(ShapeHost* host, const std::string& toolId)
    : ShapeBase(host), toolId(toolId) {
    borderPadding = 6.f * host->hostDpi();
    setAttr();
}

void ShapeText::setAttr() {
    auto c = host->selectedColor();
    colorValue = (static_cast<uint32_t>(c.r * 255) << 24) |
                 (static_cast<uint32_t>(c.g * 255) << 16) |
                 (static_cast<uint32_t>(c.b * 255) << 8) |
                 static_cast<uint32_t>(c.a * 255);
    fontSize = host->sliderValue(toolId);
    isBold = host->toolFlag(toolId, "bold");
    isItalic = host->toolFlag(toolId, "italic");
}

void ShapeText::makeTextLayout() {
    std::string displayStr = text.empty() ? " " : text;
    textLayout = std::make_unique<gfx::TextLayout>(displayStr, fontSize);
    textLayout->setBold(isBold);
    textLayout->setItalic(isItalic);
    auto m = textLayout->metrics();
    float w = std::max(42.f * host->hostDpi(), m.width + borderPadding * 2.f);
    float h = std::max(fontSize + borderPadding * 2.f, m.height + borderPadding * 2.f);
    rect.right = rect.left + w;
    rect.bottom = rect.top + h;
}

void ShapeText::paint(gfx::CanvasContext& ctx) {
    makeTextLayout();
    if (!textLayout) return;
    auto c = core::ColorRgba::fromRgbaHex(colorValue);
    if (!text.empty()) {
        ctx.drawText(rect.left + borderPadding, rect.top + borderPadding, *textLayout, c);
    }
    if (isEditing) {
        auto m = textLayout->metrics();
        float caretX = rect.left + borderPadding + (text.empty() ? 0.f : m.width);
        float caretY = rect.bottom - borderPadding - fontSize * 1.15f;
        caretY = std::max(rect.top + borderPadding, caretY);
        ctx.fillRect({caretX, caretY, caretX + 1.5f * host->hostDpi(), rect.bottom - borderPadding}, c);
        float dash[2] = {3.f, 3.f};
        ctx.save();
        ctx.setDash(dash, 2);
        ctx.strokeRect(rect, c, host->hostDpi());
        ctx.restore();
    }
}

void ShapeText::paintDragger(gfx::CanvasContext& ctx) {
    if (isEditing) return;
    float dash[2] = {3.f, 3.f};
    ctx.save();
    ctx.setDash(dash, 2);
    ctx.strokeRect(rect, core::ColorRgba::fromRgbaHex(colorValue), host->hostDpi());
    ctx.restore();
}

void ShapeText::mouseDown(float x, float y) {
    if (hoverDraggerIndex == -1) {
        rect = {x - borderPadding, y - borderPadding,
                x + 48.f * host->hostDpi(), y + fontSize + borderPadding * 2.f};
        hoverDraggerIndex = 1;
        startEdit();
    } else if (hoverDraggerIndex == 1) {
        pressX = x; pressY = y;
        startEdit();
    } else if (hoverDraggerIndex == 8) {
        pressX = x; pressY = y;
        finishEdit();
    } else if (hoverDraggerIndex == 0) {
        finishEdit();
    }
}

void ShapeText::mouseDrag(float x, float y) {
    if (hoverDraggerIndex != 8) return;
    float sx = x - pressX, sy = y - pressY;
    rect.left += sx; rect.top += sy; rect.right += sx; rect.bottom += sy;
    pressX = x; pressY = y;
    host->refresh();
}

void ShapeText::mouseMove(float x, float y) {
    hoverDraggerIndex = -1;
    float half = borderPadding * 0.5f + 2.f * host->hostDpi();
    if (x >= rect.left - half && x <= rect.right + half &&
        y >= rect.top - half && y <= rect.bottom + half) {
        bool edge = x <= rect.left + half || x >= rect.right - half ||
                    y <= rect.top + half || y >= rect.bottom - half;
        hoverDraggerIndex = edge ? 8 : 1;
    } else if (isEditing) {
        hoverDraggerIndex = 0;
    }
}

void ShapeText::mouseWheel(float, float, int delta) {
    float step = 2.f * host->hostDpi();
    float next = fontSize + (delta < 0 ? -step : step);
    float applied = host->clampSliderValue(toolId, next);
    if (applied == fontSize) return;
    fontSize = applied;
    makeTextLayout();
    host->refresh();
}

void ShapeText::setCursor() {
    if (hoverDraggerIndex == 8) host->setCursorHint(1);
    else if (hoverDraggerIndex == 1) host->setCursorHint(2);
    else host->setCursorHint(0);
}

void ShapeText::startEdit() {
    setAttr();
    isEditing = true;
    makeTextLayout();
    host->beginTextEdit(this);
    host->refresh();
}

void ShapeText::finishEdit() {
    if (!isEditing) return;
    isEditing = false;
    host->endTextEdit();
    makeTextLayout();
    host->refresh();
    if (text.empty()) {
        auto* weakHost = host;
        ShapeText* self = this;
        weakHost->postTask([weakHost, self]() {
            if (auto* hist = weakHost->history()) hist->removeShape(self);
        });
    }
}

void ShapeText::applyStyle() {
    setAttr();
    makeTextLayout();
    host->refresh();
}

} // namespace sc::shapes
