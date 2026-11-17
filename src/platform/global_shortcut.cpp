#include "screencapture/platform/global_shortcut.h"
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <map>
#include <vector>

namespace sc::platform {

namespace {

constexpr unsigned int kMaskModifiers[] = {
    0,
    LockMask,
    Mod2Mask,
    LockMask | Mod2Mask
};

struct HotkeyRecord {
    unsigned int keycode{0};
    unsigned int modifiers{0};
    std::function<void()> callback;
};

std::vector<HotkeyRecord> hotkeys;

GdkFilterReturn eventFilter(GdkXEvent* xevent, GdkEvent*, gpointer) {
    auto* ev = static_cast<XEvent*>(xevent);
    if (ev->type == KeyPress) {
        // Strip Caps Lock (LockMask) and Num Lock (Mod2Mask) before matching.
        unsigned int cleanState = ev->xkey.state & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);
        for (const auto& hk : hotkeys) {
            if (hk.keycode == ev->xkey.keycode && hk.modifiers == cleanState) {
                if (hk.callback) hk.callback();
                break;
            }
        }
    }
    return GDK_FILTER_CONTINUE;
}

unsigned int keycodeForName(Display* dpy, const std::string& name) {
    static const std::map<std::string, const char*> names = {
        {"A","a"},{"B","b"},{"C","c"},{"D","d"},{"E","e"},{"F","f"},{"G","g"},
        {"H","h"},{"I","i"},{"J","j"},{"K","k"},{"L","l"},{"M","m"},{"N","n"},
        {"O","o"},{"P","p"},{"Q","q"},{"R","r"},{"S","s"},{"T","t"},{"U","u"},
        {"V","v"},{"W","w"},{"X","x"},{"Y","y"},{"Z","z"},
        {"F1","F1"},{"F2","F2"},{"F3","F3"},{"F4","F4"},{"F5","F5"},{"F6","F6"},
        {"F7","F7"},{"F8","F8"},{"F9","F9"},{"F10","F10"},{"F11","F11"},{"F12","F12"},
        {"PRINT","Print"},{"SPACE","space"},
    };
    std::string key = name;
    for (auto& c : key) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    auto it = names.find(key);
    const char* ks = it != names.end() ? it->second : name.c_str();
    KeySym sym = XStringToKeysym(ks);
    if (sym == NoSymbol) return 0;
    return XKeysymToKeycode(dpy, sym);
}

// BadAccess seen while the grab-error handler below is installed, plus the
// handler that was in effect before we swapped ours in.
int g_grabBadAccess = 0;
int (*g_prevXErrorHandler)(Display*, XErrorEvent*) = nullptr;

// Error handler for the XGrabKey window: records BadAccess (another client
// owns the combination); anything else is forwarded to the previous handler
// (e.g. GDK's trap) so unrelated async errors keep their original accounting
// instead of being silently swallowed.
int grabErrorHandler(Display* dpy, XErrorEvent* ev) {
    if (ev->error_code == BadAccess) {
        g_grabBadAccess = BadAccess;
        return 0;
    }
    return g_prevXErrorHandler ? g_prevXErrorHandler(dpy, ev) : 0;
}
}

GlobalShortcut* GlobalShortcut::get() { static GlobalShortcut instance; return &instance; }

void GlobalShortcut::init() {
    GdkDisplay* gd = gdk_display_get_default();
    if (!gd) return;
    gdk_window_add_filter(gdk_get_default_root_window(), eventFilter, nullptr);
}

bool GlobalShortcut::registerHotkey(const std::string& spec, std::function<void()> callback) {
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    if (!dpy) return false;
    unsigned int modifiers = 0;
    std::string keyName;
    size_t start = 0;
    while (start <= spec.size()) {
        size_t plus = spec.find('+', start);
        std::string tok = spec.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        if (tok == "Ctrl" || tok == "Control") modifiers |= ControlMask;
        else if (tok == "Alt") modifiers |= Mod1Mask;
        else if (tok == "Shift") modifiers |= ShiftMask;
        else if (tok == "Super") modifiers |= Mod4Mask;
        else if (!tok.empty()) keyName = tok;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    unsigned int keycode = keycodeForName(dpy, keyName);
    if (!keycode) return false;

    Window root = DefaultRootWindow(dpy);
    // Another instance / the WM may already own this key combination: the
    // resulting BadAccess is asynchronous, and the app-wide GDK error trap
    // can never report it back (a nested pop of an outer-still-open trap
    // always returns FALSE without syncing). Swap in a private handler
    // instead — XSetErrorHandler returns the previous one, so whatever trap
    // or handler is in effect is restored untouched.
    int (*oldHandler)(Display*, XErrorEvent*) = XSetErrorHandler(grabErrorHandler);
    g_prevXErrorHandler = oldHandler;
    g_grabBadAccess = 0;
    for (unsigned int mask : kMaskModifiers) {
        XGrabKey(dpy, keycode, modifiers | mask, root, True, GrabModeAsync, GrabModeAsync);
    }
    XSync(dpy, False);
    const bool taken = g_grabBadAccess != 0;
    if (taken) {
        // Roll back the lock-mask variants that succeeded before the conflict
        // was reported: they never enter hotkeys, so unregisterAll() could
        // not release them — orphan passive grabs would block other clients
        // until this process exits. Ungrabbing a key we do not own is a no-op.
        for (unsigned int mask : kMaskModifiers) {
            XUngrabKey(dpy, keycode, modifiers | mask, root);
        }
        XSync(dpy, False);
    }
    XSetErrorHandler(oldHandler);
    g_prevXErrorHandler = nullptr;
    if (taken) return false;   // combination taken: not ours

    nextId_++;
    hotkeys.push_back({keycode, modifiers, std::move(callback)});
    return true;
}

void GlobalShortcut::unregisterAll() {
    GdkDisplay* gd = gdk_display_get_default();
    if (gd) {
        Display* dpy = GDK_DISPLAY_XDISPLAY(gd);
        if (dpy) {
            Window root = DefaultRootWindow(dpy);
            gdk_x11_display_error_trap_push(gd);
            for (const auto& hk : hotkeys) {
                for (unsigned int mask : kMaskModifiers) {
                    XUngrabKey(dpy, hk.keycode, hk.modifiers | mask, root);
                }
            }
            XFlush(dpy);
            gdk_x11_display_error_trap_pop_ignored(gd);
        }
    }
    hotkeys.clear();
}

} // namespace sc::platform
