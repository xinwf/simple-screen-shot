#pragma once
#include <gtk/gtk.h>
#include <string>

namespace sc::ui {

// Responsive follow tooltip implemented as an override-redirect, click-through
// GTK_WINDOW_POPUP with Cairo rounded dark background and white text.
class TooltipPopup {
public:
    TooltipPopup();
    ~TooltipPopup();

    void showAt(const void* owner, const std::string& text, int x, int y);
    void hide(const void* owner = nullptr);
    void bind(GtkWidget* widget, std::string text);

    // GTK signal thunks
    gboolean draw(cairo_t* cr);
    bool showNow();
    void fireDelay();

private:
    GtkWidget* window_{nullptr};
    const void* owner_{nullptr};
    std::string text_;
    int x_{0}, y_{0};
    guint delaySource_{0};
    static constexpr uint32_t kDelayMs = 180;
};

} // namespace sc::ui
