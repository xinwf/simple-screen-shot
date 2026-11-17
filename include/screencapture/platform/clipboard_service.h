#pragma once
#include "screencapture/gfx/image_surface.h"
#include <filesystem>
#include <string>

namespace sc::platform {

class ClipboardService {
public:
    static void setText(const std::string& text);
    static std::string getText();
    static void setImage(const std::shared_ptr<gfx::ImageSurface>& image);
    static void setGif(const std::filesystem::path& gifPath);
    static void setFile(const std::filesystem::path& filePath, const std::string& mimeType = "");
};

} // namespace sc::platform
