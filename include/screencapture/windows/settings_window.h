#pragma once
#include "screencapture/ui/base_window.h"

namespace sc::windows {

class SettingsWindow : public sc::ui::BaseWindow {
public:
    static void init();
    static SettingsWindow* get();
    static void dispose();

    void onCreated() override;
    void onKeyDown(uint32_t key) override;

private:
    static std::unique_ptr<SettingsWindow> instance_;
};

} // namespace sc::windows
