#pragma once
#include "screencapture/core/types.h"
#include "screencapture/gfx/image_surface.h"
#include <functional>
#include <memory>
#include <string>

namespace sc::shapes {

class ShapeBase;
class ShapeNumber;
class ShapeText;
class ShapeHistory;
class SliderStyle;

// Abstraction of the annotation host window (WinPin in xland/ScreenCapture,
// MIT License). Implemented by sc::windows::PinWindow.
class ShapeHost {
public:
    virtual ~ShapeHost() = default;

    virtual float hostDpi() const = 0;
    virtual float scale() const = 0;              // window/image scale
    virtual void refresh() = 0;

    virtual std::shared_ptr<gfx::ImageSurface> screenSurface() = 0;
    virtual ShapeHistory* history() = 0;

    // ToolSub style queries
    virtual core::ColorRgba selectedColor() const = 0;
    virtual float sliderValue(const std::string& tool) const = 0;   // returns physical px
    virtual float clampSliderValue(const std::string& tool, float physicalPx) = 0;
    virtual bool toolFlag(const std::string& tool, const std::string& key) const = 0;

    virtual bool shiftDown() const = 0;

    // Text editing support
    virtual void beginTextEdit(ShapeText* shape) = 0;
    virtual void endTextEdit() = 0;
    virtual ShapeText* editingText() const = 0;

    // Cursor hint (0=arrow, 1=sizeall, 2=ibeam, 3=cross, 4=sizenwse, 5=sizenesw, 6=sizens, 7=sizewe)
    virtual void setCursorHint(int hint) = 0;

    virtual void postTask(std::function<void()> fn) = 0;
};

// Configuration for a slider-backed tool value.
struct SliderStyle {
    std::string key;
    float min{0.f}, max{0.f}, def{0.f};
};

} // namespace sc::shapes
