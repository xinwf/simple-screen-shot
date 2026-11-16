#pragma once
#include "screencapture/core/types.h"
#include "screencapture/gfx/image_surface.h"
#include "screencapture/gfx/path_builder.h"
#include "screencapture/gfx/text_layout.h"
#include <cairo/cairo.h>
#include <memory>

namespace sc::gfx {

using CairoPtr = std::unique_ptr<cairo_t, decltype(&cairo_destroy)>;

// Two modes:
//   - borrowed window cr from GTK "draw" signal (not destroyed)
//   - offscreen cr created over an ImageSurface (owned, flushed on destruction)
class CanvasContext {
public:
    static std::shared_ptr<CanvasContext> fromWindow(cairo_t* cr);
    static std::shared_ptr<CanvasContext> fromSurface(ImageSurface& surface);

    ~CanvasContext();
    CanvasContext(const CanvasContext&) = delete;
    CanvasContext& operator=(const CanvasContext&) = delete;

    cairo_t* native() const noexcept { return cr_; }

    void save();
    void restore();
    void translate(float dx, float dy);
    void scale(float sx, float sy);
    void rotate(float radians);
    void setMatrix(float xx, float yx, float xy, float yy, float x0, float y0);
    void resetMatrix();

    void clear(const core::ColorRgba& color = {});
    void setSourceColor(const core::ColorRgba& color);
    void setLineWidth(float width);
    void setDash(const float* dashes, int count, float offset = 0.f);
    void setLineCap(cairo_line_cap_t cap);
    void setLineJoin(cairo_line_join_t join);
    void setAntialias(cairo_antialias_t aa);
    void setOperator(cairo_operator_t op);
    void setOpacity(float opacity);

    void fillRect(const core::RectF& rect, const core::ColorRgba& color);
    void strokeRect(const core::RectF& rect, const core::ColorRgba& color, float width);
    void fillRoundedRect(const core::RectF& rect, float radius, const core::ColorRgba& color);
    void strokeRoundedRect(const core::RectF& rect, float radius,
                           const core::ColorRgba& color, float width);
    void fillEllipse(float cx, float cy, float rx, float ry, const core::ColorRgba& color);
    void strokeEllipse(float cx, float cy, float rx, float ry,
                       const core::ColorRgba& color, float width);
    void drawLine(float x1, float y1, float x2, float y2,
                  const core::ColorRgba& color, float width);
    void fillPath(const PathBuilder& path, const core::ColorRgba& color);
    void strokePath(const PathBuilder& path, const core::ColorRgba& color, float width);
    void drawImage(ImageSurface& image, const core::RectF& dest, float opacity = 1.f,
                   bool nearest = false, const core::RectF* src = nullptr);
    void drawText(float x, float y, TextLayout& layout, const core::ColorRgba& color);

    bool pathContains(const PathBuilder& path, float x, float y) const;
    void clipRect(const core::RectF& rect);
    void clipPath(const PathBuilder& path);

private:
    CanvasContext(cairo_t* cr, bool owns, ImageSurface* surface);
    cairo_t* cr_{nullptr};
    bool owns_{false};
    float opacity_{1.f};
    ImageSurface* surface_{nullptr}; // non-owning, for offscreen flush
};

} // namespace sc::gfx
