#include "screencapture/platform/screen_capturer.h"
#include "screencapture/platform/x11_display.h"
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <cstring>

namespace sc::platform {

std::shared_ptr<gfx::ImageSurface> ScreenCapturer::capture(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;
    GdkWindow* root = gdk_get_default_root_window();
    if (!root) return nullptr;

    // Ensure pending X11 window unmaps/flushes are processed before grabbing pixels.
    GdkDisplay* display = gdk_display_get_default();
    if (display) gdk_display_sync(display);

    GdkPixbuf* pixbuf = gdk_pixbuf_get_from_window(root, x, y, w, h);
    if (!pixbuf) return nullptr;

    auto surface = std::make_shared<gfx::ImageSurface>(w, h);
    surface->flush();
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const int nch = gdk_pixbuf_get_n_channels(pixbuf);
    const uint8_t* src = gdk_pixbuf_read_pixels(pixbuf);
    uint8_t* dst = surface->data();
    const int dstStride = surface->stride();

    for (int row = 0; row < h; ++row) {
        const uint8_t* s = src + static_cast<size_t>(row) * rowstride;
        uint8_t* d = dst + static_cast<size_t>(row) * dstStride;
        for (int col = 0; col < w; ++col) {
            const uint8_t* px = s + static_cast<size_t>(col) * nch;
            // GdkPixbuf is always RGB[A] in bytes (px[0]=R, px[1]=G, px[2]=B),
            // whereas Cairo CAIRO_FORMAT_ARGB32 on little-endian is BGRA (d[0]=B, d[1]=G, d[2]=R, d[3]=A).
            // Force alpha = 255 so root-window visual alpha anomalies never darken desktop pixels.
            d[col * 4 + 0] = px[2]; // Blue
            d[col * 4 + 1] = px[1]; // Green
            d[col * 4 + 2] = px[0]; // Red
            d[col * 4 + 3] = 255;   // Opaque Alpha
        }
    }
    surface->markDirty();
    g_object_unref(pixbuf);
    return surface;
}

std::shared_ptr<gfx::ImageSurface> ScreenCapturer::captureDesktop() {
    auto d = X11Display::get()->virtualDesktop();
    return capture(d.left, d.top, d.width(), d.height());
}

} // namespace sc::platform
