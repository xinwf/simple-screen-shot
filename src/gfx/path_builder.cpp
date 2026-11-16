#include "screencapture/gfx/path_builder.h"

namespace sc::gfx {

void PathBuilder::replay(cairo_t* cr) const {
    if (!cr) return;
    cairo_new_path(cr);
    core::PointF cur{0.f, 0.f};
    for (const auto& cmd : cmds_) {
        switch (cmd.type) {
            case CmdType::MoveTo:
                cairo_move_to(cr, cmd.p1.x, cmd.p1.y);
                cur = cmd.p1;
                break;
            case CmdType::LineTo:
                cairo_line_to(cr, cmd.p1.x, cmd.p1.y);
                cur = cmd.p1;
                break;
            case CmdType::QuadTo: {
                // Promote quadratic to cubic (Cairo has no native quad curve).
                double c1x = cur.x + (2.0 / 3.0) * (cmd.p1.x - cur.x);
                double c1y = cur.y + (2.0 / 3.0) * (cmd.p1.y - cur.y);
                double c2x = cmd.p2.x + (2.0 / 3.0) * (cmd.p1.x - cmd.p2.x);
                double c2y = cmd.p2.y + (2.0 / 3.0) * (cmd.p1.y - cmd.p2.y);
                cairo_curve_to(cr, c1x, c1y, c2x, c2y, cmd.p2.x, cmd.p2.y);
                cur = cmd.p2;
                break;
            }
            case CmdType::CubicTo:
                cairo_curve_to(cr, cmd.p1.x, cmd.p1.y, cmd.p2.x, cmd.p2.y, cmd.p3.x, cmd.p3.y);
                cur = cmd.p3;
                break;
            case CmdType::Close:
                cairo_close_path(cr);
                break;
        }
    }
}

bool PathBuilder::contains(cairo_t* cr, float x, float y) const {
    if (!cr) return false;
    replay(cr);
    return cairo_in_fill(cr, x, y) != 0;
}

} // namespace sc::gfx
