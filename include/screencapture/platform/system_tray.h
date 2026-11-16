#pragma once
#include <functional>
#include <string>

namespace sc::platform {

class SystemTray {
public:
    static SystemTray* get();
    void init(const std::string& iconPath);
    void dispose();

    std::function<void()> onActivate;
    std::function<void()> onSettings;
    std::function<void(int)> onDelayedCapture;  // seconds; <=0 opens the custom-delay dialog
    std::function<void()> onExit;

private:
    SystemTray() = default;
    void* icon_{nullptr};
};

} // namespace sc::platform
