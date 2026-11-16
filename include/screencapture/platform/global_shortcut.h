#pragma once
#include <functional>
#include <string>

namespace sc::platform {

// Registers a global hotkey via XGrabKey and listens on the GDK X11 event loop.
class GlobalShortcut {
public:
    static GlobalShortcut* get();
    void init();
    // Parse strings like "Ctrl+Alt+A". Returns false on failure.
    bool registerHotkey(const std::string& spec, std::function<void()> callback);
    void unregisterAll();

private:
    GlobalShortcut() = default;
    unsigned int nextId_{100};
};

} // namespace sc::platform
