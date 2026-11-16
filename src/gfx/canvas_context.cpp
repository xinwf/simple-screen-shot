#include "screencapture/gfx/canvas_context.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sc::gfx {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline void applyColor(cairo_t* cr, const core::ColorRgba& c) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}
inline void roundedRectPath(cairo_t* cr, const core::RectF& r, float radius) {
    double rad = std::min({static_cast<double>(radius),
                           static_cast<double>(r.width()) * 0.5,
                           static_cast<double>(r.height()) * 0.5});
    constexpr double deg = kPi / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, r.right - rad, r.top + rad, rad, -90 * deg, 0 * deg);
    cairo_arc(cr, r.right - rad, r.bottom - rad, rad, 0 * deg, 90 * deg);
    cairo_arc(cr, r.left + rad, r.bottom - rad, rad, 90 * deg, 180 * deg);
    cairo_arc(cr, r.left + rad, r.top + rad, rad, 180 * deg, 270 * deg);
    cairo_close_path(cr);
}
}

CanvasContext::CanvasContext(cairo_t* cr, bool owns, ImageSurface* surface)
    : cr_(cr), owns_(owns), surface_(surface) {}

std::shared_ptr<CanvasContext> CanvasContext::fromWindow(cairo_t* cr) {
    return std::shared_ptr<CanvasContext>(new CanvasContext(cr, false, nullptr));
}
std::shared_ptr<CanvasContext> CanvasContext::fromSurface(ImageSurface& surface) {
    if (!surface.native()) return nullptr;
    cairo_t* cr = cairo_create(surface.native());
    if (!cr) return nullptr;
    return std::shared_ptr<CanvasContext>(new CanvasContext(cr, true, &surface));
}

CanvasContext::~CanvasContext() {
    if (cr_) {
        if (surface_) cairo_surface_flush(surface_->native());
        if (owns_) cairo_destroy(cr_);
    }
}

void CanvasContext::save() { if (cr_) cairo_save(cr_); }
void CanvasContext::restore() { if (cr_) cairo_restore(cr_); }
void CanvasContext::translate(float dx, float dy) { if (cr_) cairo_translate(cr_, dx, dy); }
void CanvasContext::scale(float sx, float sy) { if (cr_) cairo_scale(cr_, sx, sy); }
void CanvasContext::rotate(float radians) { if (cr_) cairo_rotate(cr_, radians); }
void CanvasContext::setMatrix(float xx, float yx, float xy, float yy, float x0, float y0) {
    if (!cr_) return;
    cairo_matrix_t m;
    cairo_matrix_init(&m, xx, yx, xy, yy, x0, y0);
    cairo_set_matrix(cr_, &m);
}
void CanvasContext::resetMatrix() {
    if (!cr_) return;
    cairo_matrix_t m;
    cairo_matrix_init_identity(&m);
    cairo_set_matrix(cr_, &m);
}

void CanvasContext::clear(const core::ColorRgba& color) {
    if (!cr_) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr_);
    cairo_restore(cr_);
}
void CanvasContext::setSourceColor(const core::ColorRgba& color) { if (cr_) applyColor(cr_, color); }
void CanvasContext::setLineWidth(float width) { if (cr_) cairo_set_line_width(cr_, width); }
void CanvasContext::setDash(const float* dashes, int count, float offset) {
    if (!cr_) return;
    if (dashes && count > 0) {
        std::vector<double> d(count);
        for (int i = 0; i < count; ++i) d[i] = dashes[i];
        cairo_set_dash(cr_, d.data(), count, offset);
    } else {
        cairo_set_dash(cr_, nullptr, 0, 0.0);
    }
}
void CanvasContext::setLineCap(cairo_line_cap_t cap) { if (cr_) cairo_set_line_cap(cr_, cap); }
void CanvasContext::setLineJoin(cairo_line_join_t join) { if (cr_) cairo_set_line_join(cr_, join); }
void CanvasContext::setAntialias(cairo_antialias_t aa) { if (cr_) cairo_set_antialias(cr_, aa); }
void CanvasContext::setOperator(cairo_operator_t op) { if (cr_) cairo_set_operator(cr_, op); }
void CanvasContext::setOpacity(float opacity) { opacity_ = std::clamp(opacity, 0.f, 1.f); }

void CanvasContext::fillRect(const core::RectF& rect, const core::ColorRgba& color) {
    if (!cr_ || rect.isEmpty()) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_rectangle(cr_, rect.left, rect.top, rect.width(), rect.height());
    cairo_fill(cr_);
    cairo_restore(cr_);
}
void CanvasContext::strokeRect(const core::RectF& rect, const core::ColorRgba& color, float width) {
    if (!cr_ || rect.isEmpty()) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_set_line_width(cr_, width);
    cairo_rectangle(cr_, rect.left, rect.top, rect.width(), rect.height());
    cairo_stroke(cr_);
    cairo_restore(cr_);
}
void CanvasContext::fillRoundedRect(const core::RectF& rect, float radius, const core::ColorRgba& color) {
    if (!cr_ || rect.isEmpty()) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    roundedRectPath(cr_, rect, radius);
    cairo_fill(cr_);
    cairo_restore(cr_);
}
void CanvasContext::strokeRoundedRect(const core::RectF& rect, float radius,
                                      const core::ColorRgba& color, float width) {
    if (!cr_ || rect.isEmpty()) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_set_line_width(cr_, width);
    roundedRectPath(cr_, rect, radius);
    cairo_stroke(cr_);
    cairo_restore(cr_);
}
void CanvasContext::fillEllipse(float cx, float cy, float rx, float ry, const core::ColorRgba& color) {
    if (!cr_ || rx <= 0 || ry <= 0) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_translate(cr_, cx, cy);
    cairo_scale(cr_, rx, ry);
    cairo_arc(cr_, 0.0, 0.0, 1.0, 0.0, 2.0 * kPi);
    cairo_fill(cr_);
    cairo_restore(cr_);
}
void CanvasContext::strokeEllipse(float cx, float cy, float rx, float ry,
                                  const core::ColorRgba& color, float width) {
    if (!cr_ || rx <= 0 || ry <= 0) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    // Compensate line width so it stays uniform under non-uniform scale.
    cairo_set_line_width(cr_, width / std::min(rx, ry));
    cairo_translate(cr_, cx, cy);
    cairo_scale(cr_, rx, ry);
    cairo_arc(cr_, 0.0, 0.0, 1.0, 0.0, 2.0 * kPi);
    cairo_stroke(cr_);
    cairo_restore(cr_);
}
void CanvasContext::drawLine(float x1, float y1, float x2, float y2,
                             const core::ColorRgba& color, float width) {
    if (!cr_) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_set_line_width(cr_, width);
    cairo_move_to(cr_, x1, y1);
    cairo_line_to(cr_, x2, y2);
    cairo_stroke(cr_);
    cairo_restore(cr_);
}
void CanvasContext::fillPath(const PathBuilder& path, const core::ColorRgba& color) {
    if (!cr_ || path.empty()) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    path.replay(cr_);
    cairo_fill(cr_);
    cairo_restore(cr_);
}
void CanvasContext::strokePath(const PathBuilder& path, const core::ColorRgba& color, float width) {
    if (!cr_ || path.empty()) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_set_line_width(cr_, width);
    path.replay(cr_);
    cairo_stroke(cr_);
    cairo_restore(cr_);
}
void CanvasContext::drawImage(ImageSurface& image, const core::RectF& dest, float opacity,
                              bool nearest, const core::RectF* src) {
    if (!cr_ || !image.native() || dest.isEmpty()) return;
    double sx = src ? src->left : 0.0;
    double sy = src ? src->top : 0.0;
    double sw = src ? src->width() : static_cast<double>(image.width());
    double sh = src ? src->height() : static_cast<double>(image.height());
    if (sw <= 0 || sh <= 0) return;
    cairo_save(cr_);
    cairo_rectangle(cr_, dest.left, dest.top, dest.width(), dest.height());
    cairo_clip(cr_);
    cairo_translate(cr_, dest.left, dest.top);
    cairo_scale(cr_, dest.width() / sw, dest.height() / sh);
    cairo_set_source_surface(cr_, image.native(), -sx, -sy);
    cairo_pattern_set_filter(cairo_get_source(cr_),
                             nearest ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);
    float finalAlpha = opacity * opacity_;
    if (finalAlpha >= 0.999f) cairo_paint(cr_);
    else cairo_paint_with_alpha(cr_, finalAlpha);
    cairo_restore(cr_);
}
void CanvasContext::drawText(float x, float y, TextLayout& layout, const core::ColorRgba& color) {
    if (!cr_ || !layout.native()) return;
    cairo_save(cr_);
    applyColor(cr_, color);
    cairo_move_to(cr_, x, y);
    pango_cairo_show_layout(cr_, layout.native());
    cairo_restore(cr_);
}
bool CanvasContext::pathContains(const PathBuilder& path, float x, float y) const {
    return cr_ && path.contains(cr_, x, y);
}
void CanvasContext::clipRect(const core::RectF& rect) {
    if (!cr_ || rect.isEmpty()) return;
    cairo_rectangle(cr_, rect.left, rect.top, rect.width(), rect.height());
    cairo_clip(cr_);
}
void CanvasContext::clipPath(const PathBuilder& path) {
    if (!cr_ || path.empty()) return;
    path.replay(cr_);
    cairo_clip(cr_);
}

} // namespace sc::gfx
