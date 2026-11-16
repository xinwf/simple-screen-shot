#pragma once
#include "screencapture/ui/base_window.h"
#include "screencapture/windows/cut_mask.h"
#include "screencapture/gfx/image_surface.h"
#include "screencapture/media/long_stitcher.h"
#include "screencapture/media/video_recorder.h"
#include <memory>
#include <vector>

namespace sc::windows {

class ToolCap;
class ToolLong;
class ToolVideo;

enum class CapStage { Select, Adjust, Annotate, Long, Video };

class CaptureWindow : public sc::ui::BaseWindow {
public:
    static void init();
    static CaptureWindow* get();
    static void dispose();
    static bool hasWindow();
    static void closeAll();
    static void setOverlaysVisible(bool visible);

    void onCreated() override;
    void onShown() override;
    void onPaint(gfx::CanvasContext& ctx) override;
    void onMouseDown(const core::PointF& pos, bool right) override;
    void onMouseMove(const core::PointF& pos) override;
    void onMouseUp(const core::PointF& pos, bool right) override;
    void onKeyDown(uint32_t key) override;
    void onTimer(uint32_t id) override;
    void onDestroy() override;

    std::shared_ptr<gfx::ImageSurface> getCutImage() const;
    core::RectI cutRectScreen() const;

private:
    void closeAllTools();
    void closeOtherOverlays();
    void makeToolCap();
    void startAnnotate();
    void startPin();
    void startLong();
    void startVideo();
    void startOcr();
    void startQrcode();
    bool enterByArg();
    void saveToFile();
    void copyToClipboard();
    void longStep();
    void layoutTool(sc::ui::BaseWindow* tool);
    void copyCurrentStage();
    void updateCursorForPos(const core::PointF& pos);
    void paintMagnifier(gfx::CanvasContext& ctx, const core::PointF& pos);
    void stopAndSaveVideo(bool toClipboard);

    static std::vector<std::unique_ptr<CaptureWindow>> instances_;
    static bool suppressAutoQuit_;

    core::RectI monitor_{};
    std::shared_ptr<gfx::ImageSurface> screenImage_;
    std::vector<core::RectI> visibleWindows_;
    std::unique_ptr<CutMask> cutMask_;
    std::unique_ptr<ToolCap> toolCap_;
    std::unique_ptr<ToolLong> toolLong_;
    std::unique_ptr<ToolVideo> toolVideo_;
    CapStage stage_{CapStage::Select};
    bool isPress_{false};
    bool closed_{false};
    bool isCurrentScreen_{false};
    core::PointF mousePos_{0.f, 0.f};
    core::PointF lastDownPos_{};
    int64_t lastDownTime_{0};

    // Long capture state.
    std::unique_ptr<sc::media::LongStitcher> stitcher_;
    std::shared_ptr<gfx::ImageSurface> longResult_;
    int longW_{0}, longH_{0};
    static constexpr uint32_t kScrollMsg = 18;
    static constexpr uint32_t kVideoTickMsg = 20;

    // Video recording state.
    std::unique_ptr<sc::media::VideoRecorder> videoRecorder_;
    int videoElapsedSec_{0};
};

} // namespace sc::windows
