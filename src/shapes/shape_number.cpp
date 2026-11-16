#include "screencapture/shapes/shape_number.h"
#include "screencapture/shapes/shape_history.h"
#include <algorithm>
#include <cmath>

namespace sc::shapes {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

ShapeNumber::ShapeNumber(ShapeHost* host, const std::string& toolId)
    : ShapeBase(host), toolId(toolId) {
    int maxVal = 0;
    if (auto* hist = host->history()) {
        for (const auto& s : hist->shapes) {
            if (auto* n = dynamic_cast<ShapeNumber*>(s.get())) {
                if (!n->isUndo && n->val > maxVal) maxVal = n->val;
            }
        }
    }
    val = maxVal + 1;
    applyStyle();
}

void ShapeNumber::applyStyle() {
    color = host->selectedColor();
    r = host->sliderValue(toolId);
    isFill = host->toolFlag(toolId, "fill");
    makePath();
    textLayout = std::make_unique<gfx::TextLayout>(std::to_string(val), r);
    textLayout->setBold(true);
}

core::PointF ShapeNumber::localPoint(float degrees) {
    float radians = degrees * kPi / 180.f;
    return {r * std::cos(radians), -r * std::sin(radians)};
}

core::PointF ShapeNumber::transformPoint(const core::PointF& point) {
    float radians = -angle * kPi / 180.f;
    float c = std::cos(radians), s = std::sin(radians);
    return {cx + point.x * c - point.y * s, cy + point.x * s + point.y * c};
}

void ShapeNumber::makePath() {
    path.clear();
    pathValid = false;
    if (r <= 0.f) return;
    auto start = transformPoint(localPoint(10.f));
    mid = transformPoint(localPoint(180.f));
    tip = transformPoint({r + r / 3.f, 0.f});

    auto arcSegments = [&](float fromDeg, float toDeg) {
        const int steps = 16;
        for (int i = 1; i <= steps; ++i) {
            float a = fromDeg + (toDeg - fromDeg) * (i / static_cast<float>(steps));
            auto p = transformPoint(localPoint(a));
            path.lineTo(p.x, p.y);
        }
    };
    path.moveTo(start.x, start.y);
    arcSegments(10.f, 180.f);
    arcSegments(180.f, 350.f);
    path.lineTo(tip.x, tip.y);
    path.lineTo(start.x, start.y);
    path.close();
    pathValid = true;
}

void ShapeNumber::paint(gfx::CanvasContext& ctx) {
    if (!pathValid || !textLayout) return;
    auto tm = textLayout->metrics();
    float tx = cx - tm.width * 0.5f;
    float ty = cy - tm.height * 0.5f;
    if (isFill) {
        ctx.fillPath(path, color);
        ctx.drawText(tx, ty, *textLayout, kTextColor);
    } else {
        ctx.setLineCap(CAIRO_LINE_CAP_ROUND);
        ctx.strokePath(path, color, host->hostDpi() * 1.5f);
        ctx.drawText(tx, ty, *textLayout, color);
    }
}

void ShapeNumber::paintDragger(gfx::CanvasContext& ctx) {
    if (isWheel) return;
    for (const auto& d : draggers) {
        ctx.fillRect(d, core::ColorRgba::fromHex(0xFFFFFF));
        ctx.strokeRect(d, draggerColor, host->hostDpi());
    }
}

void ShapeNumber::updateDraggers() {}

void ShapeNumber::mouseDown(float x, float y) {
    if (hoverDraggerIndex == -1) {
        cx = x; cy = y; pressX = cx; pressY = cy;
        hoverDraggerIndex = 1; // Dragging right after placing rotates the pointer tip!
        makePath();
        textLayout = std::make_unique<gfx::TextLayout>(std::to_string(val), r);
        textLayout->setBold(true);
        host->refresh();
    } else {
        pressX = x; pressY = y;
    }
}

void ShapeNumber::mouseDrag(float x, float y) {
    if (hoverDraggerIndex == 0) {
        cx += x - pressX; cy += y - pressY;
        makePath();
        pressX = x; pressY = y;
    } else if (hoverDraggerIndex == 1) {
        if (std::hypot(x - cx, y - cy) > 3.f) {
            angle = -std::atan2(y - cy, x - cx) * 180.f / kPi;
            makePath();
        }
    } else if (hoverDraggerIndex == 2) {
        float dx = x - cx, dy = y - cy;
        r = std::sqrt(dx * dx + dy * dy);
        float minR = 8.f * host->hostDpi();
        if (r < minR) r = minR;
        r = host->clampSliderValue(toolId, r);
        makePath();
        textLayout = std::make_unique<gfx::TextLayout>(std::to_string(val), r);
        textLayout->setBold(true);
    }
    host->refresh();
}

void ShapeNumber::mouseUp(float, float) {
    float half = draggerSize / 2.f;
    auto d = [half](float x, float y) { return core::RectF{x-half,y-half,x+half,y+half}; };
    draggers.clear();
    draggers = {d(cx,cy), d(tip.x,tip.y), d(mid.x,mid.y)};
}

void ShapeNumber::mouseMove(float x, float y) {
    hoverDraggerIndex = -1;
    for (int i = 0; i < static_cast<int>(draggers.size()); ++i) {
        if (isInRect(draggers[i], x, y)) { hoverDraggerIndex = i; return; }
    }
    if (hoverDraggerIndex == -1 && std::hypot(x - cx, y - cy) <= r) {
        hoverDraggerIndex = 0;
    }
    if (isWheel) {
        isWheel = false;
        mouseUp(x, y);
        host->refresh();
    }
}

void ShapeNumber::mouseWheel(float, float, int delta) {
    isWheel = true;
    float minR = 6.f * host->hostDpi();
    if (delta < 0) { if (r <= minR) return; r--; }
    else r++;
    r = host->clampSliderValue(toolId, r);
    makePath();
    textLayout = std::make_unique<gfx::TextLayout>(std::to_string(val), r);
    textLayout->setBold(true);
    host->refresh();
}

void ShapeNumber::setCursor() {
    if (hoverDraggerIndex >= 0) host->setCursorHint(1);
}

} // namespace sc::shapes
