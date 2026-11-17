#include "screencapture/platform/clipboard_service.h"
#include <gtk/gtk.h>
#include <gdk/gdkpixbuf.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace sc::platform {

namespace {
void pumpGtkEvents() {
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
}
} // namespace

void ClipboardService::setText(const std::string& text) {
    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (cb) {
        gtk_clipboard_set_text(cb, text.c_str(), static_cast<gint>(text.size()));
        gtk_clipboard_store(cb);
    }
    GtkClipboard* primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (primary) {
        gtk_clipboard_set_text(primary, text.c_str(), static_cast<gint>(text.size()));
    }
    pumpGtkEvents();
}

std::string ClipboardService::getText() {
    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (!cb) return {};
    gchar* text = gtk_clipboard_wait_for_text(cb);
    if (!text) return {};
    std::string result(text);
    g_free(text);
    return result;
}

void ClipboardService::setImage(const std::shared_ptr<gfx::ImageSurface>& image) {
    if (!image || !image->valid()) return;
    image->flush();
    const int w = static_cast<int>(image->width());
    const int h = static_cast<int>(image->height());
    const int stride = image->stride();
    const uint8_t* src = image->data();

    GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
    if (!pixbuf) return;
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    uint8_t* dst = gdk_pixbuf_get_pixels(pixbuf);
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + static_cast<size_t>(y) * stride;
        uint8_t* d = dst + static_cast<size_t>(y) * rowstride;
        for (int x = 0; x < w; ++x) {
            // Cairo BGRA -> GdkPixbuf RGBA (swap B and R)
            d[x * 4 + 0] = s[x * 4 + 2];
            d[x * 4 + 1] = s[x * 4 + 1];
            d[x * 4 + 2] = s[x * 4 + 0];
            d[x * 4 + 3] = s[x * 4 + 3];
        }
    }
    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (cb) {
        gtk_clipboard_set_image(cb, pixbuf);
        gtk_clipboard_store(cb);
    }
    g_object_unref(pixbuf);

    // Also persist to xclip in background if xclip is available so --auto-quit=true
    // keeps the image on X11 desktops without a clipboard manager.
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto tmpPng = std::filesystem::temp_directory_path() /
                  ("sc_clip_" + std::to_string(getpid()) + "_" + std::to_string(now) + ".png");
    if (image->saveToPng(tmpPng)) {
        std::string cmd = "sh -c 'if command -v xclip >/dev/null 2>&1; then "
                          "xclip -selection clipboard -t image/png -i \"" +
                          tmpPng.string() + "\" >/dev/null 2>&1 & fi'";
        int rc = std::system(cmd.c_str());
        (void)rc;
    }
    pumpGtkEvents();
}

} // namespace sc::platform
