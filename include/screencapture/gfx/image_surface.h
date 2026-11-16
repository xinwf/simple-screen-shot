#pragma once
#include "screencapture/core/types.h"
#include <cairo/cairo.h>
#include <filesystem>
#include <memory>

namespace sc::gfx {

using SurfacePtr = std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)>;

// RAII wrapper over cairo_image_surface_t (CAIRO_FORMAT_ARGB32 == BGRA8 in memory).
class ImageSurface {
public:
    ImageSurface();
    explicit ImageSurface(cairo_surface_t* surface);          // takes ownership
    ImageSurface(uint32_t width, uint32_t height);
    ImageSurface(uint32_t width, uint32_t height, const void* data, uint32_t stride);
    static std::shared_ptr<ImageSurface> fromPng(const std::filesystem::path& path);

    ImageSurface(ImageSurface&&) noexcept = default;
    ImageSurface& operator=(ImageSurface&&) noexcept = default;
    ImageSurface(const ImageSurface&) = delete;
    ImageSurface& operator=(const ImageSurface&) = delete;

    cairo_surface_t* native() const noexcept { return surface_.get(); }
    uint8_t* data() const;
    int stride() const;
    core::SizeU size() const noexcept { return {width_, height_}; }
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    bool valid() const noexcept { return surface_ != nullptr; }

    void flush() const;
    void markDirty() const;
    bool saveToPng(const std::filesystem::path& path) const;
    std::shared_ptr<ImageSurface> clone() const;
    std::shared_ptr<ImageSurface> cloneSubRect(const core::RectF& rect) const;

private:
    SurfacePtr surface_{nullptr, &cairo_surface_destroy};
    uint32_t width_{0};
    uint32_t height_{0};
};

} // namespace sc::gfx
