#pragma once
#include "screencapture/gfx/image_surface.h"
#include <string>

namespace sc::platform {

class ClipboardService {
public:
    static void setText(const std::string& text);
    static std::string getText();
    static void setImage(const std::shared_ptr<gfx::ImageSurface>& image);
};

} // namespace sc::platform
