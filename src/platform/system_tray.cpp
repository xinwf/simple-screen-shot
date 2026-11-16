#include "screencapture/platform/system_tray.h"
#include "screencapture/core/i18n.h"
#include <gtk/gtk.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace sc::platform {

SystemTray* SystemTray::get() { static SystemTray instance; return &instance; }

namespace {
struct TrayCtx {
    SystemTray* self;
    GtkStatusIcon* icon;
};
void onTrayActivate(GtkStatusIcon*, gpointer data) {
    auto* ctx = static_cast<TrayCtx*>(data);
    if (ctx->self->onActivate) ctx->self->onActivate();
}
void onSettingsItem(GtkMenuItem*, gpointer data) {
    auto* ctx = static_cast<TrayCtx*>(data);
    if (ctx->self->onSettings) ctx->self->onSettings();
}
void onExitItem(GtkMenuItem*, gpointer data) {
    auto* ctx = static_cast<TrayCtx*>(data);
    if (ctx->self->onExit) ctx->self->onExit();
}
struct DelayCtx {
    SystemTray* self;
    int seconds;
};
void onDelayedItem(GtkMenuItem*, gpointer data) {
    auto* ctx = static_cast<DelayCtx*>(data);
    if (ctx->self->onDelayedCapture) ctx->self->onDelayedCapture(ctx->seconds);
}
void freeDelayCtx(gpointer data, GClosure*) {
    delete static_cast<DelayCtx*>(data);
}

void onPopup(GtkStatusIcon*, guint, guint, gpointer data) {
    auto* ctx = static_cast<TrayCtx*>(data);
    GtkWidget* menu = gtk_menu_new();
    auto* i18n = sc::core::I18n::get();

    // Quick delayed-capture items: silent countdown, then capture.
    auto addDelayItem = [ctx, menu, i18n](int seconds) {
        std::string label = i18n->get("tray.delayN");
        if (auto pos = label.find("{n}"); pos != std::string::npos)
            label.replace(pos, 3, std::to_string(seconds));
        auto* item = gtk_menu_item_new_with_label(label.c_str());
        auto* dctx = new DelayCtx{ctx->self, seconds};
        g_signal_connect_data(item, "activate", G_CALLBACK(onDelayedItem), dctx,
                              freeDelayCtx, (GConnectFlags)0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    };
    addDelayItem(3);
    addDelayItem(5);
    addDelayItem(10);

    auto* custom = gtk_menu_item_new_with_label(i18n->get("tray.delayCustom").c_str());
    g_signal_connect(custom, "activate", G_CALLBACK(onDelayedItem),
                     (new DelayCtx{ctx->self, 0})); // 0 = open the custom-delay dialog
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), custom);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // The menu toplevel inherits an RGBA visual here, and the theme then
    // paints the menu background fully transparent (black text on nothing),
    // while the client-side shadow band renders as an opaque black frame.
    // Force an opaque, shadow-free style scoped via the widget name; the
    // two global selectors only exist for CSD toplevels, which the rest of
    // the app does not use (dialogs are WM-decorated).
    static bool styleApplied = false;
    if (!styleApplied) {
        styleApplied = true;
        auto* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider,
            "#scTrayMenu { background-color: #FFFFFF; border: 1px solid #C9C9C9; box-shadow: none; }\n"
            "#scTrayMenu menuitem { background-color: #FFFFFF; color: #222222; }\n"
            "#scTrayMenu menuitem:hover { background-color: #30A46C; color: #FFFFFF; }\n"
            "decoration { box-shadow: none; }\n"
            ".window-frame { box-shadow: none; background-color: transparent; }\n",
            -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
    }
    gtk_widget_set_name(menu, "scTrayMenu");

    auto* about = gtk_menu_item_new_with_label(i18n->get("tray.about").c_str());
    g_signal_connect(about, "activate", G_CALLBACK(onSettingsItem), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about);

    auto* exit = gtk_menu_item_new_with_label(i18n->get("tray.exit").c_str());
    g_signal_connect(exit, "activate", G_CALLBACK(onExitItem), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), exit);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

} // namespace

void SystemTray::init(const std::string& iconPath) {
    if (icon_) return;
    GtkStatusIcon* icon = gtk_status_icon_new_from_file(iconPath.c_str());
    gtk_status_icon_set_tooltip_text(icon, "Screen Capture");
    auto* ctx = new TrayCtx{this, icon};
    icon_ = ctx;
    g_signal_connect(icon, "activate", G_CALLBACK(onTrayActivate), ctx);
    g_signal_connect(icon, "popup-menu", G_CALLBACK(onPopup), ctx);
}

void SystemTray::dispose() {
    if (!icon_) return;
    auto* ctx = static_cast<TrayCtx*>(icon_);
    g_object_unref(ctx->icon);
    delete ctx;
    icon_ = nullptr;
}

} // namespace sc::platform
