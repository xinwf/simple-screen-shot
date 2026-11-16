#pragma once

namespace sc::platform {

class InputInjector {
public:
    // Inject a mouse wheel event downward (button 5) `clicks` times at current pointer.
    static void scrollWheelDown(int clicks);
    static void scrollWheelUp(int clicks);

    static void setPointer(int x, int y);
};

} // namespace sc::platform
