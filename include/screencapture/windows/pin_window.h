#pragma once
#include "screencapture/ui/base_window.h"
#include "screencapture/shapes/shape_host.h"
#include "screencapture/shapes/shape_history.h"
#include "screencapture/gfx/image_surface.h"
#include "screencapture/shapes/shape_text.h"
#include <functional>
#include <vector>

namespace sc::windows {

class ToolMain;
class ToolSub;

class PinWindow : public sc::ui::BaseWindow, public sc::shapes::ShapeHost {
public:
    static PinWindow* init(const std::shared_ptr<gfx::ImageSurface>& image, int x, int y,
                           bool inPlaceAnnotate = false);
    static std::vector<std::unique_ptr<PinWindow>>& windows();
    static bool hasWindow();
    static void dispose();
    static void raiseAll();
    static void closeInPlace();
    static bool tryHandleToolbarClick(int screenX, int screenY);

    PinWindow(const std::shared_ptr<gfx::ImageSurface>& image, int x, int y, bool inPlaceAnnotate = false);
    ~PinWindow() override;

    void onCreated() override;
    void onPaint(gfx::CanvasContext& ctx) override;
    void onMouseDown(const core::PointF& pos, bool right) override;
    void onMouseMove(const core::PointF& pos) override;
    void onMouseUp(const core::PointF& pos, bool right) override;
    void onMouseWheel(const core::PointF& pos, int delta) override;
    void onKeyDown(uint32_t key) override;
    void onTextInput(const std::string& utf8) override;
    void onTimer(uint32_t id) override;
    void onDestroy() override;
    void onShown() override;

    std::shared_ptr<gfx::ImageSurface> getImagePixels() const;

    // ShapeHost implementation
    float hostDpi() const override { return sc::ui::BaseWindow::dpi; }
    float scale() const override { return scale_; }
    void refresh() override { BaseWindow::refresh(); }
    std::shared_ptr<gfx::ImageSurface> screenSurface() override { return screenImage_; }
    sc::shapes::ShapeHistory* history() override { return history_.get(); }
    core::ColorRgba selectedColor() const override;
    float sliderValue(const std::string& tool) const override;
    float clampSliderValue(const std::string& tool, float physicalPx) override;
    bool toolFlag(const std::string& tool, const std::string& key) const override;
    bool shiftDown() const override;
    void beginTextEdit(sc::shapes::ShapeText* shape) override;
    void endTextEdit() override;
    sc::shapes::ShapeText* editingText() const override { return editingText_; }
    void setCursorHint(int hint) override;
    void postTask(std::function<void()> fn) override;
    float shadowMargin() const;

private:
    core::PointF toImgPos(const core::PointF& pos) const;
    void layoutTools();
    void updateToolSubState();
    void detachToDesktopPin();
    void saveToFile();
    void copyToClipboard();

    std::shared_ptr<gfx::ImageSurface> screenImage_;
    std::unique_ptr<sc::shapes::ShapeHistory> history_;
    std::unique_ptr<ToolMain> toolMain_;
    std::unique_ptr<ToolSub> toolSub_;
    sc::shapes::ShapeBase* shapeHover_{nullptr};
    sc::shapes::ShapeBase* newShape_{nullptr};
    sc::shapes::ShapeText* editingText_{nullptr};
    bool inPlaceAnnotate_{false};
    bool isMouseDown_{false}, hasDragged_{false};
    bool dragging_{false};
    bool showScaleTip_{false};
    core::PointF pressPos_{};
    core::PointF lastClickPos_{};
    int64_t lastClickTime_{0};
    int dragStartWindowX_{0}, dragStartWindowY_{0};
    int dragStartMouseX_{0}, dragStartMouseY_{0};
    float scale_{1.f};
    static constexpr uint32_t kHoverTimer = 100;
    static constexpr uint32_t kScaleTipTimer = 101;
};

} // namespace sc::windows
