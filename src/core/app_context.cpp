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

void AppContext::runBackground(std::function<void()> work) {
    if (!work || shouldQuit_) return;
    auto task = std::make_unique<BgTask>();
    BgTask* raw = task.get();
    raw->thread = std::thread([raw, work = std::move(work)] {
        work();
        raw->done.store(true);
    });
    std::lock_guard<std::mutex> lk(bgMutex_);
    // Reap finished workers first so the registry stays small, then insert.
    for (auto it = bgTasks_.begin(); it != bgTasks_.end();) {
        if ((*it)->done.load()) {
            (*it)->thread.join();
            it = bgTasks_.erase(it);
        } else {
            ++it;
        }
    }
    bgTasks_.push_back(std::move(task));
}

std::string AppContext::arg(const std::string& key, const std::string& def) const {
    auto it = args_.find(key);
    return it != args_.end() ? it->second : def;
}
bool AppContext::hasArg(const std::string& key) const { return args_.find(key) != args_.end(); }

void AppContext::quit(int code) {
    if (shouldQuit_.exchange(true)) return;
    exitCode_ = code;
    // Let worker jobs finish before the process starts tearing down: they
    // reference singletons that must stay alive while any worker runs. The
    // results they post via postTask() are consumed on the way out too.
    std::vector<std::unique_ptr<BgTask>> pending;
    {
        std::lock_guard<std::mutex> lk(bgMutex_);
        pending.swap(bgTasks_);
    }
    for (auto& t : pending) {
        if (t->thread.joinable()) t->thread.join();
    }
    // Workers joined above may have posted completion tasks (e.g. a finished
    // recording's save dialog) while we waited; gtk_main_quit() would drop
    // them silently and the export result would be lost. Dispatch the queued
    // events/idles now — bounded, so a task that keeps re-posting cannot
    // stall the exit. Re-entrant quit() calls are no-ops (exchange above).
    for (int i = 0; i < 256 && gtk_events_pending(); ++i) {
        gtk_main_iteration_do(FALSE);
    }
    onBeforeQuit.emit();
    if (gtk_main_level() > 0) gtk_main_quit();
}

void AppContext::run() { gtk_main(); }

} // namespace sc::core
