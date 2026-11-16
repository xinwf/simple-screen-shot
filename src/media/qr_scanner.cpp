#include "screencapture/media/qr_scanner.h"
#include <quirc.h>
#include <glib.h>

namespace sc::media {

static std::string normalizeEncoding(const uint8_t* data, int len) {
    // Strict UTF-8 first; fall back from Shift-JIS via GLib iconv.
    if (g_utf8_validate(reinterpret_cast<const gchar*>(data), len, nullptr)) {
        return std::string(reinterpret_cast<const char*>(data), static_cast<size_t>(len));
    }
    GError* err = nullptr;
    gchar* converted = g_convert(reinterpret_cast<const gchar*>(data), len,
                                 "UTF-8", "SHIFT_JIS", nullptr, nullptr, &err);
    if (converted) {
        std::string result(converted);
        g_free(converted);
        return result;
    }
    if (err) g_error_free(err);
    return std::string(reinterpret_cast<const char*>(data), static_cast<size_t>(len));
}

std::vector<std::string> QrScanner::scan(const std::shared_ptr<gfx::ImageSurface>& image) {
    std::vector<std::string> results;
    if (!image || !image->valid()) return results;
    image->flush();
    int w = static_cast<int>(image->width()), h = static_cast<int>(image->height());
    int stride = image->stride();
    const uint8_t* data = image->data();

    struct quirc* q = quirc_new();
    if (!q) return results;
    if (quirc_resize(q, w, h) < 0) { quirc_destroy(q); return results; }
    uint8_t* gray = quirc_begin(q, nullptr, nullptr);
    // quirc grayscale formula (R*77 + G*150 + B*29) >> 8
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = data + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; ++x) {
            gray[static_cast<size_t>(y) * w + x] =
                static_cast<uint8_t>((row[x*4+2] * 77 + row[x*4+1] * 150 + row[x*4] * 29) >> 8);
        }
    }
    quirc_end(q);
    int count = quirc_count(q);
    for (int i = 0; i < count; ++i) {
        struct quirc_code code;
        struct quirc_data payload;
        quirc_extract(q, i, &code);
        if (quirc_decode(&code, &payload) == QUIRC_SUCCESS) {
            results.push_back(normalizeEncoding(payload.payload, payload.payload_len));
        } else {
            // Try mirrored code.
            quirc_flip(&code);
            if (quirc_decode(&code, &payload) == QUIRC_SUCCESS) {
                results.push_back(normalizeEncoding(payload.payload, payload.payload_len));
            }
        }
    }
    quirc_destroy(q);
    return results;
}

std::string QrScanner::scanJoined(const std::shared_ptr<gfx::ImageSurface>& image) {
    auto list = scan(image);
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) out += '\n';
        out += list[i];
    }
    return out;
}

} // namespace sc::media
