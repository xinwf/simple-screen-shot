#include "screencapture/core/app_context.h"
#include <gtk/gtk.h>

namespace sc::core {

AppContext* AppContext::get() { static AppContext instance; return &instance; }

void AppContext::init(int argc, char** argv) {
    args_.clear();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) {
            auto eq = a.find('=');
            if (eq != std::string::npos) {
                args_[a.substr(0, eq)] = a.substr(eq + 1);
            } else {
                args_[a] = "true";
                // support "--flag value" form for non-boolean flags
                if ((a == "--enter" || a == "--lang") && i + 1 < argc) {
                    args_[a] = argv[++i];
                }
            }
        }
    }
}

namespace {
gboolean idleDispatch(gpointer data) {
    auto* fn = static_cast<std::function<void()>*>(data);
    if (*fn) (*fn)();
    delete fn;
    return G_SOURCE_REMOVE;
}
}

void AppContext::postTask(std::function<void()> fn) {
    auto* heap = new std::function<void()>(std::move(fn));
    g_idle_add(idleDispatch, heap);
}

std::string AppContext::arg(const std::string& key, const std::string& def) const {
    auto it = args_.find(key);
    return it != args_.end() ? it->second : def;
}
bool AppContext::hasArg(const std::string& key) const { return args_.find(key) != args_.end(); }

void AppContext::quit(int code) {
    if (shouldQuit_) return;
    shouldQuit_ = true;
    exitCode_ = code;
    onBeforeQuit.emit();
    if (gtk_main_level() > 0) gtk_main_quit();
}

void AppContext::run() { gtk_main(); }

} // namespace sc::core
