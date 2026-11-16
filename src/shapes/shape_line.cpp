#include "screencapture/shapes/shape_line.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sc::shapes {

ShapeLine::ShapeLine(ShapeHost* host, const std::string& toolId)
    : ShapeBase(host), toolId(toolId) {
    applyStyle();
}

void ShapeLine::applyStyle() {
    color = host->selectedColor();
    if (host->toolFlag(toolId, "semiTransparent")) color.a = 0.5f;
    strokeWidth = host->sliderValue(toolId);
}

void ShapeLine::makePath() {
    path.clear();
    pathValid = false;
    if (linePoints.empty()) return;
    path.moveTo(linePoints[0].x, linePoints[0].y);
    if (linePoints.size() > 1) path.linesTo(std::vector<core::PointF>(linePoints.begin() + 1, linePoints.end()));
    pathValid = true;
}

float ShapeLine::pointToSegmentDistance(const core::PointF& p, const core::PointF& a, const core::PointF& b) {
    float abx = b.x - a.x, aby = b.y - a.y;
    float apx = p.x - a.x, apy = p.y - a.y;
    float ab2 = abx * abx + aby * aby;
    float t = (ab2 == 0.f) ? 0.f : (apx * abx + apy * aby) / ab2;
    t = std::max(0.f, std::min(1.f, t));
    float cx = a.x + t * abx, cy = a.y + t * aby;
    float dx = p.x - cx, dy = p.y - cy;
    return std::sqrt(dx * dx + dy * dy);
}

void ShapeLine::hitTest(const core::PointF& mousePos) {
    float outer = strokeWidth * 0.5f + 3.f * host->hostDpi();
    float minDist = std::numeric_limits<float>::max();
    for (size_t i = 0; i + 1 < linePoints.size(); ++i) {
        minDist = std::min(minDist, pointToSegmentDistance(mousePos, linePoints[i], linePoints[i + 1]));
    }
    if (linePoints.size() == 1) minDist = std::hypot(mousePos.x - linePoints[0].x, mousePos.y - linePoints[0].y);
    if (minDist <= outer) hoverDraggerIndex = 8;
}

void ShapeLine::updateDraggers() {
    draggers.clear();
    if (linePoints.empty()) return;
    const float half = draggerSize / 2.f;
    auto d = [half](const core::PointF& p) {
        return core::RectF{p.x - half, p.y - half, p.x + half, p.y + half};
    };
    draggers.push_back(d(linePoints.front()));
    draggers.push_back(d(linePoints.back()));
}

void ShapeLine::paint(gfx::CanvasContext& ctx) {
    if (!pathValid) return;
    ctx.setLineCap(CAIRO_LINE_CAP_ROUND);
    ctx.setLineJoin(CAIRO_LINE_JOIN_ROUND);
    ctx.strokePath(path, color, strokeWidth);
}

void ShapeLine::paintDragger(gfx::CanvasContext& ctx) {
    for (const auto& d : draggers) {
        ctx.fillRect(d, core::ColorRgba::fromHex(0xFFFFFF));
        ctx.strokeRect(d, draggerColor, host->hostDpi());
    }
}

void ShapeLine::mouseDown(float x, float y) {
    if (hoverDraggerIndex == -1) {
        linePoints.push_back({x, y});
        if (host->shiftDown()) linePoints.push_back({x, y});
        makePath();
        hoverDraggerIndex = 1;
    } else if (hoverDraggerIndex == 8) {
        pressX = x; pressY = y;
    }
    host->refresh();
}

void ShapeLine::mouseDrag(float x, float y) {
    if (hoverDraggerIndex == 0) {
        if (host->shiftDown() && !linePoints.empty()) linePoints[0] = {x, y};
        else linePoints.insert(linePoints.begin(), {x, y});
        makePath();
    } else if (hoverDraggerIndex == 1) {
        if (host->shiftDown() && !linePoints.empty()) linePoints.back() = {x, y};
        else linePoints.push_back({x, y});
        makePath();
    } else if (hoverDraggerIndex == 8) {
        float sx = x - pressX, sy = y - pressY;
        for (auto& p : linePoints) { p.x += sx; p.y += sy; }
        makePath();
        pressX = x; pressY = y;
    }
    host->refresh();
}

void ShapeLine::mouseUp(float, float) { updateDraggers(); }

void ShapeLine::mouseMove(float x, float y) {
    hoverDraggerIndex = -1;
    updateDraggers();
    for (int i = 0; i < static_cast<int>(draggers.size()); ++i) {
        if (isInRect(draggers[i], x, y)) { hoverDraggerIndex = i; return; }
    }
    hitTest({x, y});
}

void ShapeLine::mouseWheel(float, float, int delta) {
    float step = host->hostDpi();
    float next = strokeWidth + (delta < 0 ? -step : step);
    float applied = host->clampSliderValue(toolId, next);
    if (applied == strokeWidth) return;
    strokeWidth = applied;
    host->refresh();
}

void ShapeLine::setCursor() {
    if (hoverDraggerIndex >= 0) host->setCursorHint(1);
}

} // namespace sc::shapes
