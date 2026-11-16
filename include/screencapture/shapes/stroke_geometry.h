#pragma once
#include "screencapture/core/types.h"
#include "screencapture/gfx/path_builder.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sc::shapes::detail {

inline float pointToSegmentDistance(const core::PointF& p, const core::PointF& a, const core::PointF& b) {
    float abx = b.x - a.x, aby = b.y - a.y;
    float apx = p.x - a.x, apy = p.y - a.y;
    float ab2 = abx * abx + aby * aby;
    float t = ab2 == 0.f ? 0.f : (apx * abx + apy * aby) / ab2;
    t = std::max(0.f, std::min(1.f, t));
    float cx = a.x + t * abx, cy = a.y + t * aby;
    float dx = p.x - cx, dy = p.y - cy;
    return std::sqrt(dx * dx + dy * dy);
}

inline int strokeHitTest(const std::vector<core::PointF>& pts, const core::PointF& mouse,
                         float strokeWidth, float dpi, float draggerSize) {
    float outer = strokeWidth * 0.5f + dpi;
    float inner = outer - draggerSize / 2.f;
    float minDist = std::numeric_limits<float>::max();
    if (pts.size() == 1) {
        minDist = std::hypot(mouse.x - pts[0].x, mouse.y - pts[0].y);
    } else {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            minDist = std::min(minDist, pointToSegmentDistance(mouse, pts[i], pts[i + 1]));
        }
    }
    return (minDist <= outer && minDist >= inner) ? 8 : -1;
}

inline void buildStrokePath(gfx::PathBuilder& path, const std::vector<core::PointF>& pts) {
    path.clear();
    if (pts.empty()) return;
    path.moveTo(pts[0].x, pts[0].y);
    if (pts.size() > 1) path.linesTo(std::vector<core::PointF>(pts.begin() + 1, pts.end()));
}

} // namespace sc::shapes::detail
