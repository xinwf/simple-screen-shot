#include "screencapture/shapes/shape_arrow.h"
#include <algorithm>
#include <cmath>

namespace sc::shapes {

namespace {
float distanceToSegment(const core::PointF& p, const core::PointF& a, const core::PointF& b) {
    float abx = b.x - a.x, aby = b.y - a.y;
    float apx = p.x - a.x, apy = p.y - a.y;
    float ab2 = abx * abx + aby * aby;
    float t = ab2 == 0.f ? 0.f : (apx * abx + apy * aby) / ab2;
    t = std::max(0.f, std::min(1.f, t));
    float dx = p.x - (a.x + t * abx), dy = p.y - (a.y + t * aby);
    return std::sqrt(dx * dx + dy * dy);
}
}

ShapeArrow::ShapeArrow(ShapeHost* host, const std::string& toolId)
    : ShapeBase(host), toolId(toolId) {
    applyStyle();
}

void ShapeArrow::applyStyle() {
    color = host->selectedColor();
    arrowSize = host->sliderValue(toolId) * 4.f;
    isFill = host->toolFlag(toolId, "fill");
    if (pathValid) makeArrow();
}

void ShapeArrow::constrainToEightDirections(float anchorX, float anchorY, float mouseX, float mouseY,
                                            float& targetX, float& targetY) {
    float dx = mouseX - anchorX, dy = mouseY - anchorY;
    float absX = std::fabs(dx), absY = std::fabs(dy);
    constexpr float kTan = 0.41421356237f;
    if (absY <= absX * kTan) {
        targetX = anchorX + dx; targetY = anchorY;
    } else if (absX <= absY * kTan) {
        targetX = anchorX; targetY = anchorY + dy;
    } else {
        float span = absX > absY ? absX : absY;
        targetX = anchorX + (dx >= 0.f ? span : -span);
        targetY = anchorY + (dy >= 0.f ? span : -span);
    }
}

void ShapeArrow::makeArrow() {
    path.clear();
    pathValid = false;
    float dx = endX - startX, dy = endY - startY;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.f) {
        path.moveTo(startX, startY);
        pathValid = true;
        return;
    }
    float ux = dx / length, uy = dy / length;
    float vx = -uy, vy = ux;
    float v1 = arrowSize / 4.f;
    float v2 = arrowSize * 2.f / 3.f;
    path.moveTo(startX, startY);
    path.lineTo(endX - arrowSize * ux - v1 * vx, endY - arrowSize * uy - v1 * vy);
    path.lineTo(endX - (arrowSize + v1) * ux - v2 * vx, endY - (arrowSize + v1) * uy - v2 * vy);
    path.lineTo(endX, endY);
    path.lineTo(endX - (arrowSize + v1) * ux + v2 * vx, endY - (arrowSize + v1) * uy + v2 * vy);
    path.lineTo(endX - arrowSize * ux + v1 * vx, endY - arrowSize * uy + v1 * vy);
    path.close();
    pathValid = true;
}

void ShapeArrow::updateDraggers() {
    draggers.clear();
    float half = draggerSize / 2.f;
    auto d = [half](float x, float y) { return core::RectF{x-half,y-half,x+half,y+half}; };
    draggers = {d(startX,startY), d(endX,endY)};
}

void ShapeArrow::paint(gfx::CanvasContext& ctx) {
    if (!pathValid) return;
    if (isFill) ctx.fillPath(path, color);
    else {
        ctx.setLineCap(CAIRO_LINE_CAP_ROUND);
        ctx.setLineJoin(CAIRO_LINE_JOIN_ROUND);
        ctx.strokePath(path, color, host->hostDpi() * 1.5f);
    }
}

void ShapeArrow::paintDragger(gfx::CanvasContext& ctx) {
    for (const auto& d : draggers) {
        ctx.fillRect(d, core::ColorRgba::fromHex(0xFFFFFF));
        ctx.strokeRect(d, draggerColor, host->hostDpi());
    }
}

void ShapeArrow::mouseDown(float x, float y) {
    if (hoverDraggerIndex == -1) {
        startX = endX = x; startY = endY = y;
        makeArrow();
        hoverDraggerIndex = 1;
    } else if (hoverDraggerIndex == 8) {
        pressX = x; pressY = y;
    }
    host->refresh();
}

void ShapeArrow::mouseDrag(float x, float y) {
    if (hoverDraggerIndex == 0) {
        if (host->shiftDown()) constrainToEightDirections(endX, endY, x, y, startX, startY);
        else { startX = x; startY = y; }
        makeArrow();
    } else if (hoverDraggerIndex == 1) {
        if (host->shiftDown()) constrainToEightDirections(startX, startY, x, y, endX, endY);
        else { endX = x; endY = y; }
        makeArrow();
    } else if (hoverDraggerIndex == 8) {
        float sx = x - pressX, sy = y - pressY;
        startX += sx; startY += sy; endX += sx; endY += sy;
        makeArrow();
        pressX = x; pressY = y;
    }
    host->refresh();
}

void ShapeArrow::mouseUp(float, float) { updateDraggers(); }

void ShapeArrow::mouseMove(float x, float y) {
    hoverDraggerIndex = -1;
    updateDraggers();
    for (int i = 0; i < static_cast<int>(draggers.size()); ++i) {
        if (isInRect(draggers[i], x, y)) { hoverDraggerIndex = i; return; }
    }
    if (pathValid) {
        float dist = distanceToSegment({x, y}, {startX, startY}, {endX, endY});
        if (dist <= arrowSize) hoverDraggerIndex = 8;
    }
}

void ShapeArrow::mouseWheel(float, float, int delta) {
    float step = 4.f * host->hostDpi();
    float next = arrowSize + (delta < 0 ? -step : step);
    float applied = host->clampSliderValue(toolId, next / 4.f) * 4.f;
    if (applied == arrowSize) return;
    arrowSize = applied;
    makeArrow();
    host->refresh();
}

void ShapeArrow::setCursor() { host->setCursorHint(1); }

} // namespace sc::shapes
