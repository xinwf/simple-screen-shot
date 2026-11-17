#include "screencapture/ui/base_window.h"
#include "screencapture/core/app_context.h"
#include <gdk/gdkx.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <tuple>

namespace sc::ui {

namespace {
gboolean sigDraw(GtkWidget*, cairo_t* cr, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleDraw(cr);
}
gboolean sigButtonPress(GtkWidget*, GdkEventButton* e, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleButtonPress(e);
}
gboolean sigButtonRelease(GtkWidget*, GdkEventButton* e, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleButtonRelease(e);
}
gboolean sigKeyRelease(GtkWidget*, GdkEventKey* e, gpointer data) {
    static_cast<BaseWindow*>(data)->handleKeyRelease(e);
    return TRUE;
}

gboolean sigMotion(GtkWidget*, GdkEventMotion* e, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleMotion(e);
}
gboolean sigLeave(GtkWidget*, GdkEventCrossing* e, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleLeave(e);
}
gboolean sigScroll(GtkWidget*, GdkEventScroll* e, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleScroll(e);
}
gboolean sigKeyPress(GtkWidget*, GdkEventKey* e, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleKeyPress(e);
}
gboolean sigConfigure(GtkWidget*, GdkEventConfigure* e, gpointer data) {
    return static_cast<BaseWindow*>(data)->handleConfigure(e);
}
void sigImCommit(GtkIMContext*, gchar* str, gpointer data) {
    if (str) static_cast<BaseWindow*>(data)->handleImCommit(str);
}
void sigDestroy(GtkWidget*, gpointer data) {
    static_cast<BaseWindow*>(data)->handleDestroy();
}
gboolean timerDispatch(gpointer data) {
    auto* p = static_cast<std::pair<BaseWindow*, uint32_t>*>(data);
    p->first->onTimerFired(p->second, p);
    return G_SOURCE_REMOVE;
}
} // namespace

BaseWindow::BaseWindow() {
    GdkScreen* screen = gdk_screen_get_default();
    if (screen) {
        double res = gdk_screen_get_resolution(screen);
        if (res > 0.0) dpi = static_cast<float>(res / 96.0);
        if (dpi < 1.f) dpi = 1.f;
    }
    body = std::make_unique<WidgetNode>(this);
}

BaseWindow::~BaseWindow() { close(); }

void BaseWindow::create(std::string title_, core::RectF bounds, const std::vector<WindowFlag>& flags) {
    title = std::move(title_);
    x = static_cast<int>(bounds.left);
    y = static_cast<int>(bounds.top);
    width = std::max(1.f, bounds.width());
    height = std::max(1.f, bounds.height());

    bool topmost = false, tool = false, noActivate = false, transparent = false, opaque = false;
    for (auto f : flags) {
        switch (f) {
            case WindowFlag::Topmost: topmost = true; break;
            case WindowFlag::ToolWindow: tool = true; break;
            case WindowFlag::NoActivate: noActivate = true; break;
            case WindowFlag::TransparentInput: transparent = true; break;
            case WindowFlag::Opaque: opaque = true; break;
        }
    }

    isPopup_ = (tool || noActivate || topmost);
    window_ = gtk_window_new(isPopup_ ? GTK_WINDOW_POPUP : GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_widget_set_app_paintable(window_, TRUE);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_NONE);

    if (topmost) gtk_window_set_keep_above(GTK_WINDOW(window_), TRUE);
    if (tool) {
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window_), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(window_), TRUE);
    }

    GdkScreen* screen = gtk_widget_get_screen(window_);
    if (screen && !opaque) {
        if (GdkVisual* visual = gdk_screen_get_rgba_visual(screen)) {
            gtk_widget_set_visual(window_, visual);
        }
    }

    drawingArea_ = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(window_), drawingArea_);

    constexpr gint kEvents =
        GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
        GDK_SCROLL_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_STRUCTURE_MASK;

    gtk_widget_add_events(window_, kEvents);
    gtk_widget_add_events(drawingArea_, kEvents);
    gtk_widget_set_can_focus(drawingArea_, TRUE);

    g_signal_connect(drawingArea_, "draw", G_CALLBACK(sigDraw), this);
    g_signal_connect(drawingArea_, "button-press-event", G_CALLBACK(sigButtonPress), this);
    g_signal_connect(drawingArea_, "button-release-event", G_CALLBACK(sigButtonRelease), this);
    g_signal_connect(drawingArea_, "motion-notify-event", G_CALLBACK(sigMotion), this);
    g_signal_connect(drawingArea_, "leave-notify-event", G_CALLBACK(sigLeave), this);
    g_signal_connect(drawingArea_, "scroll-event", G_CALLBACK(sigScroll), this);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(sigKeyPress), this);
    g_signal_connect(window_, "key-release-event", G_CALLBACK(sigKeyRelease), this);
    g_signal_connect(window_, "configure-event", G_CALLBACK(sigConfigure), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(sigDestroy), this);

    if (noActivate) {
        gtk_window_set_focus_on_map(GTK_WINDOW(window_), FALSE);
    }

    imContext_ = gtk_im_multicontext_new();
    g_signal_connect(imContext_, "commit", G_CALLBACK(sigImCommit), this);

    tooltip_ = std::make_unique<TooltipPopup>();

    onCreated();

    int wInt = std::max(1, static_cast<int>(std::lround(width)));
    int hInt = std::max(1, static_cast<int>(std::lround(height)));
    gtk_widget_set_size_request(window_, wInt, hInt);
    gtk_widget_set_size_request(drawingArea_, wInt, hInt);
    gtk_window_resize(GTK_WINDOW(window_), wInt, hInt);
    gtk_window_move(GTK_WINDOW(window_), x, y);

    layout();

    gtk_widget_realize(window_);
    if (GdkWindow* gw = gtk_widget_get_window(window_)) {
        gdk_window_set_override_redirect(gw, isPopup_ ? TRUE : FALSE);
        gdk_window_move_resize(gw, x, y, wInt, hInt);
        if (imContext_) gtk_im_context_set_client_window(imContext_, gw);
    }

    gtk_widget_show_all(window_);
    if (GdkWindow* gw = gtk_widget_get_window(window_)) {
        gdk_window_move_resize(gw, x, y, wInt, hInt);
        gdk_window_raise(gw);
    }

    if (!noActivate) {
        gtk_widget_grab_focus(drawingArea_);
    }
    if (transparent) {
        setMouseTransparent(true);
    }
    onShown();
}

void BaseWindow::layout() {
    if (body) {
        body->x = 0.f;
        body->y = 0.f;
        body->width = width;
        body->height = height;
        body->computeLayout(0.f, 0.f, width, height, dpi);
    }
    refresh();
}

void BaseWindow::relocateWindow(int nx, int ny, int nw, int nh) {
    x = nx;
    y = ny;
    nw = std::max(1, nw);
    nh = std::max(1, nh);
    width = static_cast<float>(nw);
    height = static_cast<float>(nh);
    if (window_) {
        gtk_widget_set_size_request(window_, nw, nh);
        if (drawingArea_) gtk_widget_set_size_request(drawingArea_, nw, nh);
        gtk_window_move(GTK_WINDOW(window_), nx, ny);
        gtk_window_resize(GTK_WINDOW(window_), nw, nh);
        if (gtk_widget_get_realized(window_)) {
            if (GdkWindow* gw = gtk_widget_get_window(window_)) {
                gdk_window_move_resize(gw, nx, ny, nw, nh);
                gdk_window_raise(gw);
            }
            if (drawingArea_ && gtk_widget_get_realized(drawingArea_)) {
                if (GdkWindow* dgw = gtk_widget_get_window(drawingArea_)) {
                    if (dgw != gtk_widget_get_window(window_)) {
                        gdk_window_move_resize(dgw, 0, 0, nw, nh);
                    }
                }
            }
        }
    }
    layout();
}

void BaseWindow::grabKeyboardFocus() {
    if (!window_ || !gtk_widget_get_realized(window_)) return;
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window xw = GDK_WINDOW_XID(gtk_widget_get_window(window_));
    XSetInputFocus(dpy, xw, RevertToPointerRoot, CurrentTime);
    XFlush(dpy);
    if (imContext_) gtk_im_context_focus_in(imContext_);
}

void BaseWindow::show() {
    if (!window_) return;
    int wInt = std::max(1, static_cast<int>(std::lround(width)));
    int hInt = std::max(1, static_cast<int>(std::lround(height)));
    gtk_widget_set_size_request(window_, wInt, hInt);
    if (drawingArea_) gtk_widget_set_size_request(drawingArea_, wInt, hInt);
    gtk_widget_show_all(window_);
    if (GdkWindow* gw = gtk_widget_get_window(window_)) {
        gdk_window_move_resize(gw, x, y, wInt, hInt);
        gdk_window_raise(gw);
    }
    layout();
}

void BaseWindow::hide() {
    if (tooltip_) tooltip_->hide();
    if (window_) gtk_widget_hide(window_);
}

void BaseWindow::close() {
    if (tooltip_) {
        tooltip_->hide();
        tooltip_.reset();
    }
    if (imContext_) {
        gtk_im_context_focus_out(imContext_);
        gtk_im_context_set_client_window(imContext_, nullptr);
        g_object_unref(imContext_);
        imContext_ = nullptr;
    }
    for (auto& [id, entry] : timers_) {
        if (entry.source) g_source_remove(entry.source);
        delete static_cast<std::pair<BaseWindow*, uint32_t>*>(entry.context);
    }
    timers_.clear();
    hovered_ = nullptr;
    activeSlider_ = nullptr;
    if (window_) {
        GtkWidget* tmp = window_;
        window_ = nullptr;
        drawingArea_ = nullptr;
        gtk_widget_destroy(tmp);
    }
}

void BaseWindow::refresh() {
    if (drawingArea_) gtk_widget_queue_draw(drawingArea_);
}

void BaseWindow::setTitle(const std::string& t) {
    title = t;
    if (window_) gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
}

void BaseWindow::move(int nx, int ny) {
    x = nx;
    y = ny;
    if (!window_) return;
    gtk_window_move(GTK_WINDOW(window_), nx, ny);
    if (gtk_widget_get_realized(window_)) {
        if (GdkWindow* gw = gtk_widget_get_window(window_)) {
            gdk_window_move(gw, nx, ny);
            gdk_window_raise(gw);
        }
    }
}

void BaseWindow::resize(uint32_t w, uint32_t h) {
    if (w < 1 || h < 1) return;
    width = static_cast<float>(w);
    height = static_cast<float>(h);
    if (window_) {
        gtk_widget_set_size_request(window_, static_cast<int>(w), static_cast<int>(h));
        if (drawingArea_) {
            gtk_widget_set_size_request(drawingArea_, static_cast<int>(w), static_cast<int>(h));
        }
        gtk_window_resize(GTK_WINDOW(window_), static_cast<int>(w), static_cast<int>(h));
        if (gtk_widget_get_realized(window_)) {
            if (GdkWindow* gw = gtk_widget_get_window(window_)) {
                gdk_window_resize(gw, static_cast<int>(w), static_cast<int>(h));
            }
        }
    }
    layout();
}

void BaseWindow::raise() {
    if (!window_ || !gtk_widget_get_realized(window_)) return;
    if (GdkWindow* gw = gtk_widget_get_window(window_)) {
        gdk_window_raise(gw);
    }
}

void BaseWindow::setSize(float logicW, float logicH) {
    if (logicW <= 0.f || logicH <= 0.f || dpi <= 0.f) return;
    resize(static_cast<uint32_t>(std::lround(logicW * dpi)),
           static_cast<uint32_t>(std::lround(logicH * dpi)));
}

void BaseWindow::setCursorByHint(int hint) {
    if (!window_ || !gtk_widget_get_realized(window_) || hint == lastCursorHint_) return;
    lastCursorHint_ = hint;
    GdkDisplay* display = gdk_display_get_default();
    if (!display) return;
    const char* name = "default";
    switch (hint) {
        case 1: name = "move"; break;
        case 2: name = "text"; break;
        case 3: name = "crosshair"; break;
        case 4: name = "nwse-resize"; break;
        case 5: name = "nesw-resize"; break;
        case 6: name = "ns-resize"; break;
        case 7: name = "ew-resize"; break;
        case 8: name = "pointer"; break;
        default: name = "default"; break;
    }
    GdkCursor* cursor = gdk_cursor_new_from_name(display, name);
    if (GdkWindow* gw = gtk_widget_get_window(window_)) {
        gdk_window_set_cursor(gw, cursor);
    }
    if (cursor) g_object_unref(cursor);
}

void BaseWindow::setMouseTransparent(bool transparent) {
    if (!realized()) return;
    GdkWindow* gw = gtk_widget_get_window(window_);
    if (!gw) return;
    if (transparent) {
        cairo_region_t* empty = cairo_region_create();
        gdk_window_input_shape_combine_region(gw, empty, 0, 0);
        cairo_region_destroy(empty);
    } else {
        gdk_window_input_shape_combine_region(gw, nullptr, 0, 0);
    }
}

void BaseWindow::hollowRect(const core::RectF& hole) {
    if (!realized()) return;
    GdkWindow* gw = gtk_widget_get_window(window_);
    if (!gw) return;
    cairo_rectangle_int_t full{0, 0, static_cast<int>(width), static_cast<int>(height)};
    // Inset by 2px so the selection border drawn around `hole` remains visible
    cairo_rectangle_int_t cut{
        static_cast<int>(hole.left) + 2,
        static_cast<int>(hole.top) + 2,
        std::max(1, static_cast<int>(hole.width()) - 4),
        std::max(1, static_cast<int>(hole.height()) - 4)};
    cairo_region_t* region = cairo_region_create_rectangle(&full);
    cairo_region_subtract_rectangle(region, &cut);
    gdk_window_shape_combine_region(gw, region, 0, 0);
    gdk_window_input_shape_combine_region(gw, region, 0, 0);
    cairo_region_destroy(region);
}

void BaseWindow::restoreInputShape() {
    if (!realized()) return;
    if (GdkWindow* gw = gtk_widget_get_window(window_)) {
        gdk_window_shape_combine_region(gw, nullptr, 0, 0);
        gdk_window_input_shape_combine_region(gw, nullptr, 0, 0);
    }
}

// Hover/slider tracking points into the widget tree; callers that rebuild
// body->children must drop the stale pointers first, or the next motion event
// dynamic_casts through freed nodes (crash on recording start).
void BaseWindow::resetHoverTracking() {
    hovered_ = nullptr;
    activeSlider_ = nullptr;
    if (tooltip_) tooltip_->hide();
}

void BaseWindow::setTimer(uint32_t intervalMs, uint32_t id) {
    killTimer(id);
    auto* ctx = new std::pair<BaseWindow*, uint32_t>(this, id);
    guint src = g_timeout_add(intervalMs, timerDispatch, ctx);
    timers_[id] = TimerEntry{src, ctx};
}

void BaseWindow::killTimer(uint32_t id) {
    auto it = timers_.find(id);
    if (it != timers_.end()) {
        if (it->second.source) g_source_remove(it->second.source);
        delete static_cast<std::pair<BaseWindow*, uint32_t>*>(it->second.context);
        timers_.erase(it);
    }
}

void BaseWindow::onTimerFired(uint32_t id, void* context) {
    auto it = timers_.find(id);
    if (it != timers_.end() && it->second.context == context) {
        timers_.erase(it);
    }
    onTimer(id);
    onTimerEvent.emit(id);
    delete static_cast<std::pair<BaseWindow*, uint32_t>*>(context);
}

gboolean BaseWindow::handleDraw(cairo_t* cr) {
    auto ctx = gfx::CanvasContext::fromWindow(cr);
    onPaint(*ctx);
    if (body) body->paint(*ctx, dpi);
    return TRUE;
}

gboolean BaseWindow::handleButtonPress(GdkEventButton* e) {
    if (e->type != GDK_BUTTON_PRESS) return TRUE;

    if (tooltip_) tooltip_->hide();
    core::PointF pt{static_cast<float>(e->x), static_cast<float>(e->y)};
    bool right = (e->button == 3);
    if (!right && body) {
        WidgetNode* hit = body->hitTest(pt.x, pt.y);
        while (hit && !dynamic_cast<Button*>(hit) && !dynamic_cast<Slider*>(hit)) {
            hit = hit->parent;
        }
        if (auto* slider = dynamic_cast<Slider*>(hit)) {
            activeSlider_ = slider;
            float newVal = slider->valueAt(pt.x);
            if (newVal != slider->getValue()) {
                slider->setValue(newVal);
                slider->onValueChanged.emit(slider, slider->getValue());
            }
            if (tooltip_) {
                tooltip_->showAt(slider, std::to_string(static_cast<int>(std::round(slider->getValue()))),
                                 x + static_cast<int>(pt.x), y + static_cast<int>(slider->y));
            }
            refresh();
            return TRUE;
        }
        if (auto* btn = dynamic_cast<Button*>(hit)) {
            btn->onClick.emit(btn);
            return TRUE;
        }
    }
    onMouseDown(pt, right);
    onMouseDownEvent.emit(pt, right);
    return TRUE;
}

gboolean BaseWindow::handleButtonRelease(GdkEventButton* e) {
    if (activeSlider_) {
        activeSlider_ = nullptr;
        refresh();
        return TRUE;
    }
    core::PointF pt{static_cast<float>(e->x), static_cast<float>(e->y)};
    bool right = (e->button == 3);
    onMouseUp(pt, right);
    onMouseUpEvent.emit(pt, right);
    return TRUE;
}

gboolean BaseWindow::handleMotion(GdkEventMotion* e) {
    core::PointF pt{static_cast<float>(e->x), static_cast<float>(e->y)};
    if (activeSlider_) {
        float newVal = activeSlider_->valueAt(pt.x);
        if (newVal != activeSlider_->getValue()) {
            activeSlider_->setValue(newVal);
            activeSlider_->onValueChanged.emit(activeSlider_, activeSlider_->getValue());
            refresh();
        }
        if (tooltip_) {
            tooltip_->showAt(activeSlider_,
                             std::to_string(static_cast<int>(std::round(activeSlider_->getValue()))),
                             x + static_cast<int>(pt.x), y + static_cast<int>(activeSlider_->y));
        }
        return TRUE;
    }

    bool hoverChanged = false;
    if (body) {
        WidgetNode* rawHit = body->hitTest(pt.x, pt.y);
        WidgetNode* hit = rawHit;
        while (hit && !dynamic_cast<Button*>(hit) && !dynamic_cast<Slider*>(hit)) {
            hit = hit->parent;
        }
        if (hit != hovered_) {
            if (auto* old = dynamic_cast<Button*>(hovered_)) {
                old->hovered = false;
                old->onLeave.emit(old);
            }
            hovered_ = hit;
            if (auto* cur = dynamic_cast<Button*>(hovered_)) {
                cur->hovered = true;
                cur->onEnter.emit(cur);
                setCursorByHint(8); // pointer hand cursor on buttons
                if (tooltip_ && !cur->tooltip.empty()) {
                    tooltip_->showAt(cur, cur->tooltip,
                                     x + static_cast<int>(cur->x + cur->width * 0.5f),
                                     y + static_cast<int>(cur->y));
                } else if (tooltip_) {
                    tooltip_->hide();
                }
            } else if (auto* sl = dynamic_cast<Slider*>(hovered_)) {
                setCursorByHint(8);
                if (tooltip_) {
                    int valUnderMouse = static_cast<int>(std::round(sl->valueAt(pt.x)));
                    tooltip_->showAt(sl, std::to_string(valUnderMouse),
                                     x + static_cast<int>(pt.x), y + static_cast<int>(sl->y));
                }
            } else {
                if (tooltip_) tooltip_->hide();
            }
            hoverChanged = true;
        } else if (auto* sl = dynamic_cast<Slider*>(hovered_)) {
            if (tooltip_) {
                int valUnderMouse = static_cast<int>(std::round(sl->valueAt(pt.x)));
                tooltip_->showAt(sl, std::to_string(valUnderMouse),
                                 x + static_cast<int>(pt.x), y + static_cast<int>(sl->y));
            }
        }
    }
    onMouseMove(pt);
    onMouseMoveEvent.emit(pt);
    if (hoverChanged && drawingArea_) gtk_widget_queue_draw(drawingArea_);
    return TRUE;
}

gboolean BaseWindow::handleLeave(GdkEventCrossing*) {
    if (hovered_) {
        if (auto* old = dynamic_cast<Button*>(hovered_)) {
            old->hovered = false;
            old->onLeave.emit(old);
        }
        hovered_ = nullptr;
        if (tooltip_) tooltip_->hide();
        if (drawingArea_) gtk_widget_queue_draw(drawingArea_);
    }
    return FALSE;
}

void BaseWindow::connectWindowSideEvents() {
    if (!window_) return;
    gtk_widget_add_events(window_, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                       GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(window_, "motion-notify-event", G_CALLBACK(sigMotion), this);
    g_signal_connect(window_, "button-press-event", G_CALLBACK(sigButtonPress), this);
    g_signal_connect(window_, "button-release-event", G_CALLBACK(sigButtonRelease), this);
}

gboolean BaseWindow::handleScroll(GdkEventScroll* e) {
    core::PointF pt{static_cast<float>(e->x), static_cast<float>(e->y)};
    int dir = 0;
    if (e->direction == GDK_SCROLL_UP) dir = 1;
    else if (e->direction == GDK_SCROLL_DOWN) dir = -1;
    else if (e->direction == GDK_SCROLL_SMOOTH) {
        if (e->delta_y < -0.01) dir = 1;
        else if (e->delta_y > 0.01) dir = -1;
    }
    if (dir != 0) onMouseWheel(pt, dir);
    return TRUE;
}

gboolean BaseWindow::handleKeyPress(GdkEventKey* e) {
    // Track modifier state from the event stream BEFORE the IM filter: the IM
    // (fcitx5 etc.) may consume modifier key presses entirely.
    if (e->keyval == GDK_KEY_Shift_L || e->keyval == GDK_KEY_Shift_R) shiftHeld_ = true;
    // Allow GtkIMContext to process character/IME composition first unless Ctrl/Alt or Escape is pressed
    bool ctrlOrAlt = (e->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) != 0;
    if (imContext_ && !ctrlOrAlt &&
        e->keyval != GDK_KEY_Escape &&
        e->keyval != GDK_KEY_BackSpace &&
        e->keyval != GDK_KEY_Delete &&
        e->keyval != GDK_KEY_Return &&
        e->keyval != GDK_KEY_KP_Enter) {
        if (gtk_im_context_filter_keypress(imContext_, e)) {
            return TRUE;
        }
    }
    lastKeyState_ = e->state;
    if (e->keyval == GDK_KEY_Shift_L || e->keyval == GDK_KEY_Shift_R) shiftHeld_ = true;
    onKeyDown(e->keyval);
    onKeyDownEvent.emit(e->keyval);
    return TRUE;
}

void BaseWindow::handleKeyRelease(GdkEventKey* e) {
    fprintf(stderr, "[dbg] KR keyval=0x%x\n", e->keyval);
    if (e->keyval == GDK_KEY_Shift_L || e->keyval == GDK_KEY_Shift_R) shiftHeld_ = false;
}

void BaseWindow::handleImCommit(const char* str) {
    if (!str || !*str) return;
    std::string s(str);
    onTextInput(s);
    onTextInputEvent.emit(s);
}

gboolean BaseWindow::handleConfigure(GdkEventConfigure* e) {
    if (!isPopup_) {
        if (width != static_cast<float>(e->width) || height != static_cast<float>(e->height)) {
            width = static_cast<float>(e->width);
            height = static_cast<float>(e->height);
            layout();
            onSizeChanged();
            onSizeChangedEvent.emit();
        }
    }
    return FALSE;
}

void BaseWindow::handleDestroy() {
    onDestroy();
    onDestroyed.emit();
}

} // namespace sc::ui
