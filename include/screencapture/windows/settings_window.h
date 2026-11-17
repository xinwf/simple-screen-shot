#pragma once
#include "screencapture/ui/base_window.h"
#include <memory>
#include <string>

namespace sc::ui {
class TextBox;
class Label;
class Button;
}

namespace sc::windows {

class SettingsWindow : public sc::ui::BaseWindow {
public:
    static void init();
    static SettingsWindow* get();
    static void dispose();
    void present();

    void onCreated() override;
    void onMouseDown(const core::PointF& pos, bool right) override;
    void onMouseMove(const core::PointF& pos) override;
    void onMouseUp(const core::PointF& pos, bool right) override;
    void onKeyDown(uint32_t key) override;
    void onTextInput(const std::string& utf8) override;

private:
    void rebuildUI();
    void setStatusMsg(const std::string& msg, uint32_t color);
    bool applyDirChanges();
    // Shared by the apply button and the Enter key: trims the raw text,
    // re-registers the global grab, persists it, and rolls back to the
    // previously stored key when the combination is invalid or taken.
    bool applyHotkey(const std::string& rawKey);
    void resetHotkey();
    sc::ui::TextBox* focusedField() const;
    void focusField(sc::ui::TextBox* box);

    static std::unique_ptr<SettingsWindow> instance_;

    sc::ui::TextBox* hotkeyBox_{nullptr};
    sc::ui::WidgetNode* hotkeyField_{nullptr};
    sc::ui::TextBox* dirBox_{nullptr};
    sc::ui::WidgetNode* dirField_{nullptr};
    sc::ui::Label* statusLabel_{nullptr};
    sc::ui::TextBox* pressBox_{nullptr};

    bool dragging_{false};
    float winX0_{0.f}, winY0_{0.f};
    float absPressX_{0.f}, absPressY_{0.f};
    bool fieldPressing_{false};
};

} // namespace sc::windows
