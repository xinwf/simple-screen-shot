#include "screencapture/shapes/shape_rect.h"
#include <algorithm>

namespace sc::shapes {

ShapeRect::ShapeRect(ShapeHost* host, const std::string& toolId)
    : ShapeBase(host), toolId(toolId) {
    applyStyle();
}

void ShapeRect::applyStyle() {
    color = host->selectedColor();
    strokeWidth = host->sliderValue(toolId);
    isFill = host->toolFlag(toolId, "fill");
}

void ShapeRect::updateDraggers() {
    draggers.clear();
    const float half = draggerSize / 2.f;
    const float l = rect.left, t = rect.top, r = rect.right, b = rect.bottom;
    const float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
    auto d = [half](float x, float y) {
        return core::RectF{x - half, y - half, x + half, y + half};
    };
    draggers = {
        d(l, t), d(cx, t), d(r, t), d(r, cy),
        d(r, b), d(cx, b), d(l, b), d(l, cy),
    };
}

void ShapeRect::paint(gfx::CanvasContext& ctx) {
    if (rect.isEmpty()) return;
    if (isFill) ctx.fillRect(rect, color);
    else ctx.strokeRect(rect, color, strokeWidth);
}

void ShapeRect::paintDragger(gfx::CanvasContext& ctx) {
    for (const auto& d : draggers) {
        ctx.fillRect(d, core::ColorRgba::fromHex(0xFFFFFF));
        ctx.strokeRect(d, draggerColor, host->hostDpi());
    }
}

void ShapeRect::mouseDown(float x, float y) {
    if (hoverDraggerIndex == -1) {
        pressX = x; pressY = y;
        rect = {x, y, x, y};
        hoverDraggerIndex = 4;
    } else if (hoverDraggerIndex == 0) { pressX = rect.right; pressY = rect.bottom; }
    else if (hoverDraggerIndex == 1) { pressY = rect.bottom; }
    else if (hoverDraggerIndex == 2) { pressX = rect.left; pressY = rect.bottom; }
    else if (hoverDraggerIndex == 3) { pressX = rect.left; }
    else if (hoverDraggerIndex == 4) { pressX = rect.left; pressY = rect.top; }
    else if (hoverDraggerIndex == 5) { pressY = rect.top; }
    else if (hoverDraggerIndex == 6) { pressX = rect.right; pressY = rect.top; }
    else if (hoverDraggerIndex == 7) { pressX = rect.right; }
    else if (hoverDraggerIndex == 8) { pressX = x - rect.left; pressY = y - rect.top; }
}

void ShapeRect::mouseDrag(float x, float y) {
    int i = hoverDraggerIndex;
    if (i == -1) return;
    if (i == 0 || i == 2 || i == 4 || i == 6) {
        rect.left = std::min(pressX, x); rect.right = std::max(pressX, x);
        rect.top = std::min(pressY, y); rect.bottom = std::max(pressY, y);
    } else if (i == 1 || i == 5) {
        rect.top = std::min(pressY, y); rect.bottom = std::max(pressY, y);
    } else if (i == 3 || i == 7) {
        rect.left = std::min(pressX, x); rect.right = std::max(pressX, x);
    } else if (i == 8) {
        float w = rect.width(), h = rect.height();
        rect.left = x - pressX; rect.top = y - pressY;
        rect.right = rect.left + w; rect.bottom = rect.top + h;
    }
    if (i != 8 && host->shiftDown()) {
        float w = rect.width(), h = rect.height();
        if (w > h) rect.bottom = rect.top + w;
        else rect.right = rect.left + h;
    }
    host->refresh();
}

void ShapeRect::mouseUp(float, float) { updateDraggers(); }

void ShapeRect::mouseMove(float x, float y) {
    hoverDraggerIndex = -1;
    for (int i = 0; i < static_cast<int>(draggers.size()); ++i) {
        if (isInRect(draggers[i], x, y)) { hoverDraggerIndex = i; return; }
    }
    const float half = isFill ? 4.f * host->hostDpi() : strokeWidth / 2.f + 3.f * host->hostDpi();
    if (x >= rect.left - half && x <= rect.right + half &&
        y >= rect.top - half && y <= rect.bottom + half) {
        if (isFill || x <= rect.left + half || x >= rect.right - half ||
            y <= rect.top + half || y >= rect.bottom - half) {
            hoverDraggerIndex = 8;
        }
    }
}

void ShapeRect::mouseWheel(float, float, int delta) {
    if (isFill) return;
    float step = host->hostDpi();
    float next = strokeWidth + (delta < 0 ? -step : step);
    float applied = host->clampSliderValue(toolId, next);
    if (applied == strokeWidth) return;
    strokeWidth = applied;
    host->refresh();
}

void ShapeRect::setCursor() {
    int i = hoverDraggerIndex;
    if (i == 0 || i == 4) host->setCursorHint(4);
    else if (i == 2 || i == 6) host->setCursorHint(5);
    else if (i == 1 || i == 5) host->setCursorHint(6);
    else if (i == 3 || i == 7) host->setCursorHint(7);
    else if (i == 8) host->setCursorHint(1);
}

} // namespace sc::shapes
