#pragma once
#include "screencapture/shapes/shape_base.h"
#include <memory>
#include <string>
#include <vector>

namespace sc::shapes {

class ShapeMosaic : public ShapeBase {
public:
    ShapeMosaic(ShapeHost* host, const std::string& toolId);
    void paint(gfx::CanvasContext& ctx) override;
    void paintDragger(gfx::CanvasContext& ctx) override;
    void mouseDrag(float x, float y) override;
    void mouseDown(float x, float y) override;
    void mouseUp(float x, float y) override;
    void mouseMove(float x, float y) override;
    void mouseWheel(float, float, int delta) override;
    void setCursor() override;
    void applyStyle() override;

    core::RectF rect{};
    float strokeWidth{28.f};

private:
    void makePath();
    void updateDraggers();
    void resetMosaic();
    void buildMosaicBitmap();
    void mosaicPixels(uint8_t* bits, uint32_t pitch, uint32_t width, uint32_t height, int blockSize);

    std::vector<core::RectF> draggers;
    std::vector<core::PointF> linePoints;
    gfx::PathBuilder path;
    bool pathValid{false};
    std::shared_ptr<gfx::ImageSurface> mosaicSurface;
    core::PointF mosaicOrigin{0.f, 0.f};
    float pressX{0.f}, pressY{0.f};
    bool isRect{false};
    static constexpr core::ColorRgba kPlaceholder{0.f, 1.f, 0.f, 0.38f};
    std::string toolId{"mosaic"};
};

} // namespace sc::shapes
