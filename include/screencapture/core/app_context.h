#pragma once
#include "screencapture/core/types.h"
#include <functional>
#include <map>
#include <string>

namespace sc::core {

// Application singleton: parses --key=value args, owns the main-thread task
// queue (g_idle_add) and the quit signal.
class AppContext {
public:
    static AppContext* get();

    void init(int argc, char** argv);
    void postTask(std::function<void()> fn);

    std::string arg(const std::string& key, const std::string& def = {}) const;
    bool hasArg(const std::string& key) const;
    bool autoQuit() const { return arg("--auto-quit") == "true"; }
    std::string enterMode() const { return arg("--enter"); }

    void quit(int code = 0);
    void run();
    bool shouldQuit() const noexcept { return shouldQuit_; }
    int exitCode() const noexcept { return exitCode_; }

    Event<> onBeforeQuit;

private:
    AppContext() = default;
    std::map<std::string, std::string> args_;
    bool shouldQuit_{false};
    int exitCode_{0};
};

} // namespace sc::core
