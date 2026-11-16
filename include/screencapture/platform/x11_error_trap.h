#pragma once
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

namespace sc::platform {

// Swallow transient X errors (BadMatch / BadWindow) raised by requests
// issued between push and pop_ignored — e.g. paints racing with the RGBA
// overlay windows being unmapped / reshaped / hidden. Without the trap,
// GDK's default X error handler aborts the whole process on such replies.
//
// Use popIgnored (never a synchronizing pop) so errors that arrive after
// the region's last request are still attributed to the outstanding trap.
inline void x11ErrorTrapPush() {
    gdk_x11_display_error_trap_push(gdk_display_get_default());
}
inline void x11ErrorTrapPopIgnored() {
    gdk_x11_display_error_trap_pop_ignored(gdk_display_get_default());
}

} // namespace sc::platform
