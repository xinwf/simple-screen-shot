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

    // Cairo BGRA (little-endian ARGB32) vs GdkPixbuf RGB[A]. Alpha is
    // forced to 255 so root-window visual alpha anomalies never darken
    // desktop pixels. The 4-channel path works one 32-bit word per pixel
    // (shifts/masks only) so the compiler auto-vectorises the row.
    if (nch == 4) {
        for (int row = 0; row < h; ++row) {
            const guint32* s = reinterpret_cast<const guint32*>(src + static_cast<size_t>(row) * rowstride);
            guint32* d = reinterpret_cast<guint32*>(dst + static_cast<size_t>(row) * dstStride);
            for (int col = 0; col < w; ++col) {
                const guint32 v = s[col];   // R G B A packed little-endian
                d[col] = ((v & 0x00FF0000u) >> 16) | (v & 0x0000FF00u) |
                         ((v & 0x000000FFu) << 16) | 0xFF000000u;   // B G R 255
            }
        }
    } else {
        for (int row = 0; row < h; ++row) {
            const uint8_t* s = src + static_cast<size_t>(row) * rowstride;
            uint8_t* d = dst + static_cast<size_t>(row) * dstStride;
            for (int col = 0; col < w; ++col) {
                const uint8_t* px = s + static_cast<size_t>(col) * nch;
                d[col * 4 + 0] = px[2]; // Blue
                d[col * 4 + 1] = px[1]; // Green
                d[col * 4 + 2] = px[0]; // Red
                d[col * 4 + 3] = 255;   // Opaque Alpha
            }
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
