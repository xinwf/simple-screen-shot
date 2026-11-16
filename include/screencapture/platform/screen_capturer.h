#pragma once
#include "screencapture/gfx/image_surface.h"
#include <memory>

namespace sc::platform {

class ScreenCapturer {
public:
    // Capture a region of the virtual desktop into an ARGB32 surface.
    static std::shared_ptr<gfx::ImageSurface> capture(int x, int y, int w, int h);

    // Capture the whole virtual desktop.
    static std::shared_ptr<gfx::ImageSurface> captureDesktop();
};

} // namespace sc::platform
