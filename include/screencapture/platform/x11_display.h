#pragma once
#include "screencapture/core/types.h"
#include <vector>

namespace sc::platform {

class X11Display {
public:
    static X11Display* get();
    void init();
    void refreshMonitors();

    // Virtual desktop union of all monitors (desktop coordinates).
    core::RectI virtualDesktop() const noexcept { return desktop_; }

    // Individual monitor rectangles (desktop coordinates). Each physical
    // screen must be covered by its own overlay window, never merged into one
    // spanning rectangle (the gaps between monitors would be wrong).
    const std::vector<core::RectI>& monitors() const noexcept { return monitors_; }

    // Monitor containing the given desktop point; falls back to the first.
    core::RectI monitorAt(int x, int y) const;

    // Current pointer position in desktop coordinates.
    core::PointI pointerPosition() const;

    // Top-level visible windows in Z-order (top first), desktop coordinates.
    std::vector<core::RectI> enumerateVisibleWindows() const;

    // Query current modifier state.
    bool shiftDown() const;
    bool controlDown() const;

private:
    X11Display() = default;
    core::RectI desktop_{};
    std::vector<core::RectI> monitors_;
};

} // namespace sc::platform
