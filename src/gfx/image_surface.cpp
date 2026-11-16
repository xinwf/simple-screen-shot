#include "screencapture/gfx/image_surface.h"
#include <algorithm>
#include <cstring>

namespace sc::gfx {

ImageSurface::ImageSurface() = default;

ImageSurface::ImageSurface(cairo_surface_t* surface) : surface_(surface, &cairo_surface_destroy) {
    if (surface_) {
        width_ = static_cast<uint32_t>(cairo_image_surface_get_width(surface_.get()));
        height_ = static_cast<uint32_t>(cairo_image_surface_get_height(surface_.get()));
    }
}

ImageSurface::ImageSurface(uint32_t width, uint32_t height)
    : surface_(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                          static_cast<int>(width), static_cast<int>(height)),
               &cairo_surface_destroy),
      width_(width), height_(height) {}

ImageSurface::ImageSurface(uint32_t width, uint32_t height, const void* data, uint32_t stride)
    : ImageSurface(width, height) {
    if (data && surface_ && cairo_surface_status(surface_.get()) == CAIRO_STATUS_SUCCESS) {
        flush();
        uint8_t* dst = this->data();
        const int dstStride = this->stride();
        const uint32_t copyBytes = std::min<uint32_t>(stride, static_cast<uint32_t>(dstStride));
        const auto* src = static_cast<const uint8_t*>(data);
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * dstStride,
                        src + static_cast<size_t>(y) * stride, copyBytes);
        }
        markDirty();
    }
}

std::shared_ptr<ImageSurface> ImageSurface::fromPng(const std::filesystem::path& path) {
    auto* raw = cairo_image_surface_create_from_png(path.c_str());
    if (!raw || cairo_surface_status(raw) != CAIRO_STATUS_SUCCESS) {
        if (raw) cairo_surface_destroy(raw);
        return nullptr;
    }
    return std::make_shared<ImageSurface>(raw);
}

uint8_t* ImageSurface::data() const {
    return surface_ ? cairo_image_surface_get_data(surface_.get()) : nullptr;
}
int ImageSurface::stride() const {
    return surface_ ? cairo_image_surface_get_stride(surface_.get()) : 0;
}
void ImageSurface::flush() const { if (surface_) cairo_surface_flush(surface_.get()); }
void ImageSurface::markDirty() const { if (surface_) cairo_surface_mark_dirty(surface_.get()); }

bool ImageSurface::saveToPng(const std::filesystem::path& path) const {
    if (!surface_) return false;
    flush();
    return cairo_surface_write_to_png(surface_.get(), path.c_str()) == CAIRO_STATUS_SUCCESS;
}

std::shared_ptr<ImageSurface> ImageSurface::clone() const {
    if (!surface_) return nullptr;
    flush();
    auto result = std::make_shared<ImageSurface>(width_, height_);
    const int srcStride = stride();
    const int dstStride = result->stride();
    const uint32_t bytes = std::min<uint32_t>(srcStride, dstStride);
    const uint8_t* src = data();
    uint8_t* dst = result->data();
    for (uint32_t y = 0; y < height_; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * dstStride,
                    src + static_cast<size_t>(y) * srcStride, bytes);
    }
    result->markDirty();
    return result;
}

std::shared_ptr<ImageSurface> ImageSurface::cloneSubRect(const core::RectF& rect) const {
    if (!surface_ || rect.isEmpty()) return nullptr;
    int x0 = std::max(0, static_cast<int>(rect.left));
    int y0 = std::max(0, static_cast<int>(rect.top));
    int x1 = std::min(static_cast<int>(width_), static_cast<int>(rect.right));
    int y1 = std::min(static_cast<int>(height_), static_cast<int>(rect.bottom));
    if (x0 >= x1 || y0 >= y1) return nullptr;
    const uint32_t w = static_cast<uint32_t>(x1 - x0);
    const uint32_t h = static_cast<uint32_t>(y1 - y0);
    auto result = std::make_shared<ImageSurface>(w, h);
    flush();
    const int srcStride = stride();
    const int dstStride = result->stride();
    const uint8_t* src = data();
    uint8_t* dst = result->data();
    for (uint32_t row = 0; row < h; ++row) {
        std::memcpy(dst + static_cast<size_t>(row) * dstStride,
                    src + static_cast<size_t>(y0 + row) * srcStride + static_cast<size_t>(x0) * 4,
                    static_cast<size_t>(w) * 4);
    }
    result->markDirty();
    return result;
}

} // namespace sc::gfx
