#pragma once
#include "screencapture/core/types.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sc::core {

// Application singleton: parses --key=value args, owns the main-thread task
// queue (g_idle_add), the background worker registry and the quit signal.
class AppContext {
public:
    static AppContext* get();

    void init(int argc, char** argv);
    void postTask(std::function<void()> fn);

    // Run `work` on a worker thread so blocking jobs (ffmpeg export, OCR)
    // never freeze the UI. Anything touching the singletons or the UI must
    // be handed back with postTask() from inside work. quit() waits for all
    // still-running background work, so a worker can never outlive the
    // static singletons it references.
    void runBackground(std::function<void()> work);

    std::string arg(const std::string& key, const std::string& def = {}) const;
    bool hasArg(const std::string& key) const;
    bool autoQuit() const { return arg("--auto-quit") == "true"; }
    std::string enterMode() const { return arg("--enter"); }

    void quit(int code = 0);
    void run();
    bool shouldQuit() const noexcept { return shouldQuit_.load(); }
    int exitCode() const noexcept { return exitCode_; }

    Event<> onBeforeQuit;

private:
    AppContext() = default;
    struct BgTask {
        std::thread thread;
        std::atomic<bool> done{false};
    };

    std::map<std::string, std::string> args_;
    std::atomic<bool> shouldQuit_{false};
    int exitCode_{0};
    std::mutex bgMutex_;
    std::vector<std::unique_ptr<BgTask>> bgTasks_;
};

} // namespace sc::core
