#pragma once
#include "screencapture/ui/base_window.h"
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace sc::windows {

// Base class for the floating tool bars. Buttons are self-drawn Label nodes.
class ToolBarBase : public sc::ui::BaseWindow {
public:
    std::function<void(const std::string&)> onAction;

    // Dispatch a mouse press at global screen coordinates if it falls within this bar.
    bool tryHandleScreenClick(int screenX, int screenY);

protected:
    sc::ui::Button* addButton(const std::string& id, const std::string& glyph,
                              const std::string& tip = {}, float width = 32.f,
                              bool iconFont = true);
    void addSeparator(float width = 1.f, uint32_t color = 0xDDDDDDFF);
    void buildBar(const std::vector<std::string>& ids, const std::vector<std::string>& glyphs);
};

// ToolCap: shown after selection. mark/long/video/ocr/qrcode/close/save/clipboard.
class ToolCap : public ToolBarBase {
public:
    void onCreated() override;
};

// ToolMain: annotation palette on pinned/annotated images.
class ToolMain : public ToolBarBase {
public:
    void onCreated() override;
    std::string currentTool() const { return currentTool_; }
    void cancelSelect();
    void selectTool(const std::string& id);
    float getSelectedBtnCenterX() const;

private:
    std::string currentTool_;
};

// ToolSub: secondary style bar (fill/stroke toggle, bold/italic, width/fontSize slider, 9 colors).
class ToolSub : public ToolBarBase {
public:
    static constexpr float kMainGap = 2.f;

    void onCreated() override;
    void onPaint(sc::gfx::CanvasContext& ctx) override;

    void showForTool(const std::string& toolId);
    void hideTools();
    bool hasContent() const noexcept { return hasTools_; }
    float desiredHeightPx() const noexcept { return std::floor(35.f * dpi); }

    void updatePosition(const core::RectI& workArea, int mainX, int mainY,
                        float mainW, float mainH, float btnCenterX);
    float syncSliderFromWheel(const std::string& toolId, float physicalPx);

    std::function<void()> onStyleChanged;

private:
    void rebuildControls();
    sc::ui::WidgetNode* contentNode_{nullptr};
    sc::ui::Slider* slider_{nullptr};
    std::vector<sc::ui::Button*> colorBtns_;
    std::string curToolId_;
    std::string curSliderKey_;
    float sliderMin_{1.f}, sliderMax_{20.f}, sliderVal_{2.f};
    int selectColorIndex_{0};
    bool hasTools_{false};
    bool centerOnBtn_{false};
    float arrowX_{16.f};
};

// ToolLong: pin/close/save/clipboard for long screenshots.
class ToolLong : public ToolBarBase {
public:
    void onCreated() override;
};

// ToolVideo: MP4/GIF recording bar with format/audio toggles and recording timer.
class ToolVideo : public ToolBarBase {
public:
    void onCreated() override;
    void showRecording(bool recording);
    void setTimerText(const std::string& text);

    bool isMp4() const noexcept { return formatIndex_ == 0; }
    bool useSpeaker() const noexcept { return useSpeaker_; }
    bool useMic() const noexcept { return useMic_; }
    bool isRecordingState() const noexcept { return recording_; }

private:
    void rebuildSettingBar();
    void updateToggleStyles();

    sc::ui::Button* btnMp4_{nullptr};
    sc::ui::Button* btnGif_{nullptr};
    sc::ui::Button* btnSpeaker_{nullptr};
    sc::ui::Button* btnMic_{nullptr};
    sc::ui::Label* timerLabel_{nullptr};
    int formatIndex_{0}; // 0 = MP4, 1 = GIF
    bool useSpeaker_{true};
    bool useMic_{false};
    bool recording_{false};
};

} // namespace sc::windows
