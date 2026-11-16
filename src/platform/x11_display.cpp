#include "screencapture/platform/x11_display.h"
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <algorithm>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace sc::platform {

X11Display* X11Display::get() { static X11Display instance; return &instance; }

void X11Display::init() {
    GdkDisplay* gd = gdk_display_get_default();
    if (!gd) return;
    int n = gdk_display_get_n_monitors(gd);
    monitors_.clear();
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool first = true;
    for (int i = 0; i < n; ++i) {
        GdkRectangle g{};
        gdk_monitor_get_geometry(gdk_display_get_monitor(gd, i), &g);
        if (g.width <= 0 || g.height <= 0) continue;
        monitors_.push_back({g.x, g.y, g.x + g.width, g.y + g.height});
        if (first) {
            minX = g.x; minY = g.y; maxX = g.x + g.width; maxY = g.y + g.height;
            first = false;
        } else {
            minX = std::min(minX, g.x);
            minY = std::min(minY, g.y);
            maxX = std::max(maxX, g.x + g.width);
            maxY = std::max(maxY, g.y + g.height);
        }
    }
    desktop_ = {minX, minY, maxX, maxY};
    // Guard against no monitor / degenerate geometry.
    if (desktop_.isEmpty()) {
        GdkScreen* gs = gdk_screen_get_default();
        int sw = gs ? gdk_screen_get_width(gs) : 1920;
        int sh = gs ? gdk_screen_get_height(gs) : 1080;
        desktop_ = {0, 0, sw, sh};
        monitors_.push_back(desktop_);
    }
}

core::RectI X11Display::monitorAt(int x, int y) const {
    for (const auto& r : monitors_) {
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return r;
    }
    return monitors_.empty() ? desktop_ : monitors_.front();
}

core::PointI X11Display::pointerPosition() const {
    core::PointI pt{0, 0};
    GdkDisplay* d = gdk_display_get_default();
    if (!d) return pt;
    GdkSeat* seat = gdk_display_get_default_seat(d);
    if (!seat) return pt;
    gdk_device_get_position(gdk_seat_get_pointer(seat), nullptr, &pt.x, &pt.y);
    return pt;
}

std::vector<core::RectI> X11Display::enumerateVisibleWindows() const {
    std::vector<core::RectI> result;
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    if (!dpy) return result;
    Window root = DefaultRootWindow(dpy);

    Atom netClientList = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", True);
    if (netClientList == None) return result;
    Atom actualType;
    int actualFormat;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, root, netClientList, 0, 0x7fffffff, False, XA_WINDOW,
                           &actualType, &actualFormat, &nItems, &bytesAfter, &data) != Success) {
        return result;
    }
    if (!data || actualFormat != 32) {
        if (data) XFree(data);
        return result;
    }
    auto* windows = reinterpret_cast<Window*>(data);
    for (unsigned long i = 0; i < nItems; ++i) {
        XWindowAttributes attr{};
        if (!XGetWindowAttributes(dpy, windows[i], &attr)) continue;
        if (attr.map_state != IsViewable) continue;
        if (attr.width <= 6 || attr.height <= 6) continue;
        // Skip override-redirect windows (tooltips, menus).
        if (attr.override_redirect) continue;
        int absX = 0, absY = 0;
        Window child = None;
        XTranslateCoordinates(dpy, windows[i], root, 0, 0, &absX, &absY, &child);
        result.push_back({absX, absY, absX + attr.width, absY + attr.height});
    }
    XFree(data);
    return result;
}

bool X11Display::shiftDown() const {
    GdkDisplay* d = gdk_display_get_default();
    if (!d) return false;
    GdkSeat* seat = gdk_display_get_default_seat(d);
    if (!seat) return false;
    GdkModifierType mask{};
    gdk_device_get_state(gdk_seat_get_pointer(seat),
                         gdk_screen_get_root_window(gdk_screen_get_default()), nullptr, &mask);
    return (mask & GDK_SHIFT_MASK) != 0;
}
bool X11Display::controlDown() const {
    GdkDisplay* d = gdk_display_get_default();
    if (!d) return false;
    GdkSeat* seat = gdk_display_get_default_seat(d);
    if (!seat) return false;
    GdkModifierType mask{};
    gdk_device_get_state(gdk_seat_get_pointer(seat),
                         gdk_screen_get_root_window(gdk_screen_get_default()), nullptr, &mask);
    return (mask & GDK_CONTROL_MASK) != 0;
}

} // namespace sc::platform
