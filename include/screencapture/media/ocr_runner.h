#pragma once
#include "screencapture/gfx/image_surface.h"
#include <string>

namespace sc::media {

struct OcrResult {
    std::string text;
    bool success{false}; // false = engine could not run; empty text may still be a valid OCR result
    std::string error;  // engine diagnostics when the process could not complete
};

class OcrRunner {
public:
    // Saves the image to a temp PNG, runs an OCR engine, returns UTF-8 text.
    // `pluginPath` optional: if a plugin exists there it is preferred.
    static OcrResult recognize(const std::shared_ptr<gfx::ImageSurface>& image,
                               const std::string& pluginPath = {});

    // True when an OCR engine can actually run: a plugin binary at
    // `pluginPath`, or tesseract somewhere on PATH.
    static bool available(const std::string& pluginPath = {});

    // Fast probe (plugin --check: imports only, no engine init): true when the
    // plugin reports RapidOCR support.
    static bool rapidocrAvailable(const std::string& pluginPath);
};

} // namespace sc::media
