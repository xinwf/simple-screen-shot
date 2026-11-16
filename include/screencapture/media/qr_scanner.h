#pragma once
#include "screencapture/gfx/image_surface.h"
#include <string>
#include <vector>

namespace sc::media {

class QrScanner {
public:
    // Returns decoded payloads (UTF-8). Multiple codes joined by caller if desired.
    static std::vector<std::string> scan(const std::shared_ptr<gfx::ImageSurface>& image);
    static std::string scanJoined(const std::shared_ptr<gfx::ImageSurface>& image);
};

} // namespace sc::media
