#include "screencapture/platform/clipboard_service.h"
#include <gtk/gtk.h>
#include <gdk/gdkpixbuf.h>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>

namespace sc::platform {

namespace {
void pumpGtkEvents() {
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
}

struct FileClipboardContext {
    std::filesystem::path filePath;
    std::string uri;
    std::string mimeType;
    std::vector<uint8_t> rawData;
    std::vector<uint8_t> fallbackPng;
};

enum {
    TARGET_CUSTOM_MIME,
    TARGET_IMAGE_PNG,
    TARGET_URI_LIST,
    TARGET_GNOME_COPIED,
    TARGET_UTF8,
    TARGET_PLAIN
};

void onFileClipboardGet(GtkClipboard* /*cb*/, GtkSelectionData* sel, guint info, gpointer data) {
    auto* ctx = static_cast<FileClipboardContext*>(data);
    if (!ctx) return;
    GdkAtom target = gtk_selection_data_get_target(sel);

    if (info == TARGET_CUSTOM_MIME || (!ctx->mimeType.empty() && target == gdk_atom_intern(ctx->mimeType.c_str(), FALSE))) {
        if (!ctx->rawData.empty()) {
            gtk_selection_data_set(sel, target, 8,
                                   reinterpret_cast<const guchar*>(ctx->rawData.data()),
                                   static_cast<gint>(ctx->rawData.size()));
        }
    } else if (info == TARGET_IMAGE_PNG || target == gdk_atom_intern_static_string("image/png")) {
        if (!ctx->fallbackPng.empty()) {
            gtk_selection_data_set(sel, target, 8,
                                   reinterpret_cast<const guchar*>(ctx->fallbackPng.data()),
                                   static_cast<gint>(ctx->fallbackPng.size()));
        }
    } else if (info == TARGET_URI_LIST || target == gdk_atom_intern_static_string("text/uri-list")) {
        gchar* uris[] = { const_cast<gchar*>(ctx->uri.c_str()), nullptr };
        gtk_selection_data_set_uris(sel, uris);
    } else if (info == TARGET_GNOME_COPIED || target == gdk_atom_intern_static_string("x-special/gnome-copied-files")) {
        std::string gnomeStr = "copy\n" + ctx->uri + "\n";
        gtk_selection_data_set(sel, target, 8,
                               reinterpret_cast<const guchar*>(gnomeStr.data()),
                               static_cast<gint>(gnomeStr.size()));
    } else if (info == TARGET_UTF8 || info == TARGET_PLAIN ||
               target == gdk_atom_intern_static_string("UTF8_STRING") ||
               target == gdk_atom_intern_static_string("text/plain")) {
        gtk_selection_data_set_text(sel, ctx->filePath.c_str(), static_cast<gint>(ctx->filePath.string().size()));
    }
}

void onFileClipboardClear(GtkClipboard* /*cb*/, gpointer data) {
    delete static_cast<FileClipboardContext*>(data);
}

void cleanOldClips(const std::filesystem::path& clipDir) {
    std::error_code ec;
    if (!std::filesystem::exists(clipDir, ec)) return;

    struct ClipEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime;
        uintmax_t size{0};
    };
    std::vector<ClipEntry> entries;

    const auto now = std::filesystem::file_time_type::clock::now();
    constexpr auto kMaxAge = std::chrono::hours(24 * 3);    // Auto-clean clips older than 3 days
    constexpr uintmax_t kMaxTotalBytes = 150 * 1024 * 1024; // Cap total clip cache to 150 MB
    constexpr size_t kMaxCount = 20;                        // Retain at most 20 recent clips

    uintmax_t totalBytes = 0;

    for (const auto& it : std::filesystem::directory_iterator(clipDir, ec)) {
        if (!it.is_regular_file(ec)) continue;
        auto mtime = it.last_write_time(ec);
        auto sz = it.file_size(ec);
        if (ec) continue;

        if (now - mtime > kMaxAge) {
            std::filesystem::remove(it.path(), ec);
            continue;
        }

        totalBytes += sz;
        entries.push_back({it.path(), mtime, sz});
    }

    std::sort(entries.begin(), entries.end(), [](const ClipEntry& a, const ClipEntry& b) {
        return a.mtime > b.mtime;
    });

    while (entries.size() > kMaxCount || totalBytes > kMaxTotalBytes) {
        if (entries.empty()) break;
        const auto& oldest = entries.back();
        std::filesystem::remove(oldest.path, ec);
        totalBytes = (totalBytes > oldest.size) ? (totalBytes - oldest.size) : 0;
        entries.pop_back();
    }
}

std::filesystem::path preparePersistentClipFile(const std::filesystem::path& srcPath, const std::string& prefix, const std::string& ext) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s_%04d%02d%02d_%02d%02d%02d.%s",
                  prefix.c_str(),
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  ext.c_str());

    std::filesystem::path clipDir = std::filesystem::path(g_get_user_cache_dir()) / "screencapture" / "clips";
    std::error_code ec;
    std::filesystem::create_directories(clipDir, ec);

    // Auto-prune stale or excessive clips before adding the new clip
    cleanOldClips(clipDir);

    std::filesystem::path dst = clipDir / buf;
    std::filesystem::copy_file(srcPath, dst, std::filesystem::copy_options::overwrite_existing, ec);
    return dst;
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
    if (w <= 0 || h <= 0) return;
    const int stride = image->stride();
    const uint8_t* src = image->data();
    if (!src) return;

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
                          tmpPng.string() + "\" >/dev/null 2>&1; fi'";
        g_spawn_command_line_async(cmd.c_str(), nullptr);
    }
    pumpGtkEvents();
}

void ClipboardService::setGif(const std::filesystem::path& gifPath) {
    std::error_code ec;
    if (!std::filesystem::exists(gifPath, ec)) return;

    auto persistent = preparePersistentClipFile(gifPath, "gif", "gif");

    auto* ctx = new FileClipboardContext();
    ctx->filePath = persistent;
    ctx->mimeType = "image/gif";

    std::ifstream in(persistent, std::ios::binary);
    if (in) {
        in.seekg(0, std::ios::end);
        size_t sz = in.tellg();
        in.seekg(0, std::ios::beg);
        ctx->rawData.resize(sz);
        in.read(reinterpret_cast<char*>(ctx->rawData.data()), sz);
    }

    gchar* uriStr = g_filename_to_uri(persistent.c_str(), nullptr, nullptr);
    if (uriStr) {
        ctx->uri = uriStr;
        g_free(uriStr);
    }

    GError* err = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(persistent.c_str(), &err);
    if (pixbuf) {
        gchar* buf = nullptr;
        gsize bufSize = 0;
        if (gdk_pixbuf_save_to_buffer(pixbuf, &buf, &bufSize, "png", nullptr, nullptr)) {
            ctx->fallbackPng.assign(reinterpret_cast<uint8_t*>(buf), reinterpret_cast<uint8_t*>(buf + bufSize));
            g_free(buf);
        }
        g_object_unref(pixbuf);
    }
    if (err) g_error_free(err);

    static const GtkTargetEntry gifTargets[] = {
        { (gchar*)"image/gif", 0, TARGET_CUSTOM_MIME },
        { (gchar*)"image/png", 0, TARGET_IMAGE_PNG },
        { (gchar*)"text/uri-list", 0, TARGET_URI_LIST },
        { (gchar*)"x-special/gnome-copied-files", 0, TARGET_GNOME_COPIED },
        { (gchar*)"UTF8_STRING", 0, TARGET_UTF8 },
        { (gchar*)"text/plain", 0, TARGET_PLAIN }
    };

    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (cb) {
        gtk_clipboard_set_with_data(cb, gifTargets, G_N_ELEMENTS(gifTargets),
                                    onFileClipboardGet, onFileClipboardClear, ctx);
        gtk_clipboard_set_can_store(cb, gifTargets, G_N_ELEMENTS(gifTargets));
        gtk_clipboard_store(cb);
    }

    GtkClipboard* primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (primary) {
        gtk_clipboard_set_text(primary, persistent.c_str(), static_cast<gint>(persistent.string().size()));
    }

    pumpGtkEvents();
}

void ClipboardService::setFile(const std::filesystem::path& filePath, const std::string& mimeType) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) return;

    std::string ext = filePath.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    auto persistent = preparePersistentClipFile(filePath, ext.empty() ? "file" : ext, ext);

    auto* ctx = new FileClipboardContext();
    ctx->filePath = persistent;
    ctx->mimeType = mimeType;

    gchar* uriStr = g_filename_to_uri(persistent.c_str(), nullptr, nullptr);
    if (uriStr) {
        ctx->uri = uriStr;
        g_free(uriStr);
    }

    static const GtkTargetEntry fileTargets[] = {
        { (gchar*)"text/uri-list", 0, TARGET_URI_LIST },
        { (gchar*)"x-special/gnome-copied-files", 0, TARGET_GNOME_COPIED },
        { (gchar*)"UTF8_STRING", 0, TARGET_UTF8 },
        { (gchar*)"text/plain", 0, TARGET_PLAIN }
    };

    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (cb) {
        gtk_clipboard_set_with_data(cb, fileTargets, G_N_ELEMENTS(fileTargets),
                                    onFileClipboardGet, onFileClipboardClear, ctx);
        gtk_clipboard_set_can_store(cb, fileTargets, G_N_ELEMENTS(fileTargets));
        gtk_clipboard_store(cb);
    }

    GtkClipboard* primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (primary) {
        gtk_clipboard_set_text(primary, persistent.c_str(), static_cast<gint>(persistent.string().size()));
    }

    pumpGtkEvents();
}

} // namespace sc::platform
