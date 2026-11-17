#pragma once
#include "screencapture/core/types.h"
#include "screencapture/gfx/canvas_context.h"
#include "screencapture/ui/widget_node.h"
#include "screencapture/ui/tooltip_popup.h"
#include <gtk/gtk.h>
#include <map>
#include <vector>
#include <memory>
#include <string>

namespace sc::ui {

enum class WindowFlag {
    Topmost,
    ToolWindow,
    NoActivate,
    TransparentInput,
    Opaque,
};

// GTK3 borderless window with native override-redirect (GTK_WINDOW_POPUP) support,
// GtkIMContext UTF-8 text input, and X11 visual/input shape control.
class BaseWindow {
public:
    BaseWindow();
    virtual ~BaseWindow();

    void create(std::string title, core::RectF bounds,
                const std::vector<WindowFlag>& flags = {});
    void show();
    void hide();
    void close();
    void refresh();
    void layout();

    void setTitle(const std::string& title);
    void move(int x, int y);
    void resize(uint32_t w, uint32_t h);
    void setSize(float logicW, float logicH);
    void raise();
    void grabKeyboardFocus();
    void relocateWindow(int nx, int ny, int nw, int nh);

    void setMouseTransparent(bool transparent);
    void hollowRect(const core::RectF& hole);
    void restoreInputShape();
    void setCursorByHint(int hint);
    // Drop hovered_/activeSlider_/tooltip before rebuilding body->children —
    // they point into the old tree and would dangle across the rebuild.
    void resetHoverTracking();

    void setTimer(uint32_t intervalMs, uint32_t id);
    void killTimer(uint32_t id);
    void onTimerFired(uint32_t id, void* context);
    // Modifiers (GDK_SHIFT_MASK etc.) carried by the most recent key event.
    guint lastKeyModifiers() const noexcept { return lastKeyState_; }
    // Shift state tracked from the key event stream (press/release delivered
    // to this window) — reliable even for fast synthetic key sequences,
    // unlike live server-state queries which lag the event queue.
    bool shiftHeld() const noexcept { return shiftHeld_; }

    // Connect motion / button handlers on the toplevel window as well. Needed
    // while a pointer grab is active (events then arrive on the toplevel, not
    // the drawing area), e.g. for custom window dragging.
    void connectWindowSideEvents();

    // GTK signal bridge
    gboolean handleDraw(cairo_t* cr);
    gboolean handleButtonPress(GdkEventButton* event);
    gboolean handleButtonRelease(GdkEventButton* event);
    gboolean handleMotion(GdkEventMotion* event);
    gboolean handleLeave(GdkEventCrossing* event);
    gboolean handleScroll(GdkEventScroll* event);
    gboolean handleKeyPress(GdkEventKey* event);
    gboolean handleConfigure(GdkEventConfigure* event);
    void handleImCommit(const char* str);
    void handleKeyRelease(GdkEventKey* e);
    void handleDestroy();

    virtual void onCreated() {}
    virtual void onPaint(gfx::CanvasContext&) {}
    virtual void onMouseDown(const core::PointF&, bool /*right*/) {}
    virtual void onMouseMove(const core::PointF&) {}
    virtual void onMouseUp(const core::PointF&, bool /*right*/) {}
    virtual void onMouseWheel(const core::PointF&, int /*delta*/) {}
    virtual void onKeyDown(uint32_t /*key*/) {}
    virtual void onTextInput(const std::string& /*utf8*/) {}
    virtual void onTimer(uint32_t /*id*/) {}
    virtual void onDestroy() {}
    virtual void onSizeChanged() {}
    virtual void onShown() {}

    GtkWidget* native() const noexcept { return window_; }
    GtkWidget* drawArea() const noexcept { return drawingArea_; }
    bool realized() const noexcept { return window_ && gtk_widget_get_realized(window_); }
    bool isVisible() const noexcept { return window_ && gtk_widget_get_visible(window_); }

    int x{0}, y{0};
    float width{800.f}, height{600.f};
    float dpi{1.f};
    std::string title{"ScreenCapture"};
    std::unique_ptr<WidgetNode> body;

    core::Event<> onDestroyed;
    core::Event<> onSizeChangedEvent;
    core::Event<core::PointF, bool> onMouseDownEvent;
    core::Event<core::PointF> onMouseMoveEvent;
    core::Event<core::PointF, bool> onMouseUpEvent;
    core::Event<uint32_t> onKeyDownEvent;
    core::Event<std::string> onTextInputEvent;
    core::Event<uint32_t> onTimerEvent;

private:
    struct TimerEntry {
        guint source{0};
        void* context{nullptr};
    };
    GtkWidget* window_{nullptr};
    GtkWidget* drawingArea_{nullptr};
    GtkIMContext* imContext_{nullptr};
    WidgetNode* hovered_{nullptr};
    Slider* activeSlider_{nullptr};
    bool isPopup_{false};
    int lastCursorHint_{-1};
    guint lastKeyState_{0};
    bool shiftHeld_{false};
    std::unique_ptr<TooltipPopup> tooltip_;
    std::map<uint32_t, TimerEntry> timers_;
};

} // namespace sc::ui
