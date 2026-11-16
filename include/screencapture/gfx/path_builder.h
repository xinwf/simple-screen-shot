#pragma once
#include "screencapture/core/types.h"
#include <cairo/cairo.h>
#include <vector>

namespace sc::gfx {

// Records vector path commands and replays them onto any cairo context.
class PathBuilder {
public:
    enum class CmdType { MoveTo, LineTo, QuadTo, CubicTo, Close };

    struct Command {
        CmdType type;
        core::PointF p1;
        core::PointF p2;
        core::PointF p3;
    };

    void moveTo(float x, float y) { cmds_.push_back({CmdType::MoveTo, {x, y}, {}, {}}); }
    void lineTo(float x, float y) { cmds_.push_back({CmdType::LineTo, {x, y}, {}, {}}); }
    void linesTo(const std::vector<core::PointF>& pts) {
        for (const auto& p : pts) lineTo(p.x, p.y);
    }
    void quadTo(float cx, float cy, float x, float y) {
        cmds_.push_back({CmdType::QuadTo, {cx, cy}, {x, y}, {}});
    }
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
        cmds_.push_back({CmdType::CubicTo, {c1x, c1y}, {c2x, c2y}, {x, y}});
    }
    void close() { cmds_.push_back({CmdType::Close, {}, {}, {}}); }
    void clear() { cmds_.clear(); }
    bool empty() const noexcept { return cmds_.empty(); }

    void replay(cairo_t* cr) const;
    // True if (x,y) falls inside the constructed (filled) path.
    bool contains(cairo_t* cr, float x, float y) const;

private:
    std::vector<Command> cmds_;
};

} // namespace sc::gfx
