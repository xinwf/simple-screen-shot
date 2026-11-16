#pragma once
#include "screencapture/gfx/image_surface.h"
#include <string>

namespace sc::media {

class OcrRunner {
public:
    // Saves the image to a temp PNG, runs an OCR engine, returns UTF-8 text.
    // `pluginPath` optional: if a plugin exists there it is preferred.
    static std::string recognize(const std::shared_ptr<gfx::ImageSurface>& image,
                                 const std::string& pluginPath = {});

    // True when an OCR engine can actually run: a plugin binary at
    // `pluginPath`, or tesseract somewhere on PATH.
    static bool available(const std::string& pluginPath = {});

    // Fast probe (plugin --check: imports only, no engine init): true when the
    // plugin reports RapidOCR support.
    static bool rapidocrAvailable(const std::string& pluginPath);
};

} // namespace sc::media
