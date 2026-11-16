#include "screencapture/ui/tooltip_popup.h"
#include "screencapture/gfx/text_layout.h"
#include <gdk/gdkx.h>
#include <algorithm>

namespace sc::ui {

namespace {
gboolean sigDraw(GtkWidget*, cairo_t* cr, gpointer data) {
    return static_cast<TooltipPopup*>(data)->draw(cr) ? TRUE : FALSE;
}
gboolean tooltipDelayThunk(gpointer data) {
    static_cast<TooltipPopup*>(data)->fireDelay();
    return G_SOURCE_REMOVE;
}
} // namespace

TooltipPopup::TooltipPopup() {
    window_ = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window_), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(window_), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(window_), FALSE);
    gtk_widget_set_app_paintable(window_, TRUE);
    GdkScreen* screen = gtk_widget_get_screen(window_);
    if (screen) {
        if (GdkVisual* visual = gdk_screen_get_rgba_visual(screen)) {
            gtk_widget_set_visual(window_, visual);
        }
    }
    gtk_widget_add_events(window_, GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK);
    g_signal_connect(window_, "draw", G_CALLBACK(sigDraw), this);
}

TooltipPopup::~TooltipPopup() {
    if (delaySource_) g_source_remove(delaySource_);
    if (window_) gtk_widget_destroy(window_);
}

void TooltipPopup::bind(GtkWidget* widget, std::string text) {
    g_object_set_data_full(G_OBJECT(widget), "sc-tooltip-text",
                           new std::string(std::move(text)),
                           [](gpointer p) { delete static_cast<std::string*>(p); });
}

void TooltipPopup::showAt(const void* owner, const std::string& text, int x, int y) {
    if (text.empty()) {
        hide();
        return;
    }
    bool alreadyVisible = (window_ && gtk_widget_get_visible(window_));
    bool sameOwner = (owner_ == owner);
    owner_ = owner;
    text_ = text;
    x_ = x;
    y_ = y;

    // If a tooltip is already visible on this bar, update text & position immediately
    // when gliding across buttons or dragging a slider.
    if (alreadyVisible) {
        if (delaySource_) {
            g_source_remove(delaySource_);
            delaySource_ = 0;
        }
        showNow();
        return;
    }

    if (sameOwner && delaySource_) {
        // Still waiting on the same widget/slider; keep existing timer running with updated x_/y_/text_
        return;
    }

    if (delaySource_) g_source_remove(delaySource_);
    delaySource_ = g_timeout_add(kDelayMs, tooltipDelayThunk, this);
}

void TooltipPopup::hide(const void* owner) {
    if (owner && owner_ && owner != owner_) return;
    if (delaySource_) {
        g_source_remove(delaySource_);
        delaySource_ = 0;
    }
    if (window_ && gtk_widget_get_visible(window_)) {
        gtk_widget_hide(window_);
    }
    owner_ = nullptr;
}

void TooltipPopup::fireDelay() {
    delaySource_ = 0;
    showNow();
}

bool TooltipPopup::showNow() {
    if (text_.empty() || !window_) return false;
    gfx::TextLayout layout(text_, 12.f, 400.f);
    auto m = layout.metrics();
    gint w = std::max(1, static_cast<gint>(m.width) + 16);
    gint h = std::max(1, static_cast<gint>(m.height) + 10);

    int tx = std::max(4, x_ - w / 2);
    int ty = y_ - h - 5;
    if (ty < 4) {
        ty = y_ + 36;
    }

    gtk_widget_set_size_request(window_, w, h);
    gtk_window_resize(GTK_WINDOW(window_), w, h);
    gtk_window_move(GTK_WINDOW(window_), tx, ty);
    gtk_widget_show_all(window_);
    if (GdkWindow* gw = gtk_widget_get_window(window_)) {
        gdk_window_set_override_redirect(gw, TRUE);
        // Make tooltip completely click-through / mouse-transparent so it never
        // steals hover from the toolbar button underneath!
        cairo_region_t* empty = cairo_region_create();
        gdk_window_input_shape_combine_region(gw, empty, 0, 0);
        cairo_region_destroy(empty);
        gdk_window_move_resize(gw, tx, ty, w, h);
        gdk_window_raise(gw);
    }
    gtk_widget_queue_draw(window_);
    return true;
}

gboolean TooltipPopup::draw(cairo_t* cr) {
    if (!cr || text_.empty()) return FALSE;
    gint w = gtk_widget_get_allocated_width(window_);
    gint h = gtk_widget_get_allocated_height(window_);
    gfx::TextLayout layout(text_, 12.f, 400.f);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 0.14, 0.14, 0.14, 0.94);
    double r = 4.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, r, r, r, 3.14159265, 1.5 * 3.14159265);
    cairo_arc(cr, w - r, r, r, 1.5 * 3.14159265, 2.0 * 3.14159265);
    cairo_arc(cr, w - r, h - r, r, 0.0, 0.5 * 3.14159265);
    cairo_arc(cr, r, h - r, r, 0.5 * 3.14159265, 3.14159265);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, 8, 5);
    pango_cairo_show_layout(cr, layout.native());
    cairo_restore(cr);
    return TRUE;
}

} // namespace sc::ui
