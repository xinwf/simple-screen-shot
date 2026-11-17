#pragma once
#include "screencapture/ui/base_window.h"

namespace sc::windows {

// Startup dialog shown when the app is launched normally: pick a delay in
// seconds (default 0 = immediate) and press the capture button, mirroring the
// classic gnome-screenshot "delayed screenshot" flow. The app stays in the
// tray while this dialog is open.
class DelayLauncherWindow : public sc::ui::BaseWindow {
public:
    static void init();
    static DelayLauncherWindow* get() { return instance_.get(); }
    static void dispose();
    static bool hasWindow() { return instance_ != nullptr; }

    // Silent countdown started from the tray menu: capture fires after the
    // given seconds without any dialog. seconds <= 0 opens this dialog so
    // the user can pick a custom delay.
    static void startTrayCountdown(int seconds);

    void onCreated() override;
    void onKeyDown(uint32_t key) override;
    void onMouseDown(const core::PointF& pos, bool right) override;
    void onMouseMove(const core::PointF& pos) override;
    void onMouseUp(const core::PointF& pos, bool right) override;

private:
    void startCapture();
    void updateControls();

    // Title-area window drag state, mirroring SaveDialogWindow.
    bool dragging_{false};
    float absPressX_{0.f}, absPressY_{0.f};   // absolute pointer pos at drag start
    int winX0_{0}, winY0_{0};                 // window origin at drag start

    static std::unique_ptr<DelayLauncherWindow> instance_;

    int seconds_{0};      // configured delay, 0 = capture immediately
    sc::ui::Label* valueLabel_{nullptr};
    sc::ui::Button* minusBtn_{nullptr};
    sc::ui::Button* plusBtn_{nullptr};
    sc::ui::Button* startBtn_{nullptr};
};

} // namespace sc::windows
