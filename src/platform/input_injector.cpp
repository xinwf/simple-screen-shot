#include "screencapture/platform/input_injector.h"
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/extensions/XTest.h>

namespace sc::platform {

static void fakeButton(unsigned int button, int clicks) {
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    if (!dpy) return;
    for (int i = 0; i < clicks; ++i) {
        XTestFakeButtonEvent(dpy, button, True, CurrentTime);
        XTestFakeButtonEvent(dpy, button, False, CurrentTime);
    }
    XFlush(dpy);
}

void InputInjector::scrollWheelDown(int clicks) { fakeButton(5, clicks); }
void InputInjector::scrollWheelUp(int clicks) { fakeButton(4, clicks); }

void InputInjector::setPointer(int x, int y) {
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    if (!dpy) return;
    XTestFakeMotionEvent(dpy, -1, x, y, CurrentTime);
    XFlush(dpy);
}

} // namespace sc::platform
