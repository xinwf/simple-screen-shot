#include "screencapture/platform/system_tray.h"
#include "screencapture/core/i18n.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <functional>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace sc::platform {

SystemTray* SystemTray::get() { static SystemTray instance; return &instance; }

namespace {

struct MenuActCtx {
    SystemTray* self;
    GtkWidget* menu;   // popup menu to dismiss before the action runs
    int seconds;       // <0: plain capture; >=0: delayed capture (0 = custom dialog)
};

struct DeferredInvoke {
    std::function<void()> fn;
};

gboolean runDeferred(gpointer data) {
    auto* d = static_cast<DeferredInvoke*>(data);
    if (d->fn) d->fn();
    delete d;
    return G_SOURCE_REMOVE;
}

// Close the popup menu, let the compositor repaint the area it covered, then
// run the action. The capture overlay grabs root-window pixels immediately on
// startup, so launching capture while the menu is still mapped records the
// menu into the screenshot.
void dismissMenuThen(GtkWidget* menu, std::function<void()> fn) {
    if (menu && gtk_widget_get_mapped(menu)) {
        gtk_menu_popdown(GTK_MENU(menu));
        gtk_widget_hide(menu);
    }
    if (GdkDisplay* dpy = gdk_display_get_default()) gdk_display_flush(dpy);
    g_timeout_add(220, runDeferred, new DeferredInvoke{std::move(fn)});
}

void onCaptureItem(GtkMenuItem*, gpointer data) {
    auto* ctx = static_cast<MenuActCtx*>(data);
    if (!ctx) return;
    SystemTray* self = ctx->self;
    dismissMenuThen(ctx->menu, [self]() {
        if (self && self->onActivate) self->onActivate();
    });
}

void onDelayedItem(GtkMenuItem*, gpointer data) {
    auto* ctx = static_cast<MenuActCtx*>(data);
    if (!ctx) return;
    SystemTray* self = ctx->self;
    int seconds = ctx->seconds;
    dismissMenuThen(ctx->menu, [self, seconds]() {
        if (self && self->onDelayedCapture) self->onDelayedCapture(seconds);
    });
}

void freeMenuActCtx(gpointer data, GClosure*) {
    delete static_cast<MenuActCtx*>(data);
}

// Is a legacy XEmbed tray host present? Panels such as xfce4-panel, mate-panel
// and cinnamon own the _NET_SYSTEM_TRAY_S<screen> selection. GNOME Shell
// dropped XEmbed support in 3.26 and owns nothing, so there the
// StatusNotifierItem (appindicator) path must be used instead.
bool xembedTrayHostAvailable() {
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    if (!dpy) return false;
    char atomName[64];
    std::snprintf(atomName, sizeof(atomName), "_NET_SYSTEM_TRAY_S%d", DefaultScreen(dpy));
    Atom sel = XInternAtom(dpy, atomName, False);
    return sel != None && XGetSelectionOwner(dpy, sel) != None;
}

// Function pointer signatures for libappindicator / libayatana-appindicator
typedef void* (*fn_new_with_path)(const char*, const char*, int, const char*);
typedef void* (*fn_new)(const char*, const char*, int);
typedef void (*fn_set_status)(void*, int);
typedef void (*fn_set_menu)(void*, GtkMenu*);
typedef void (*fn_set_icon_full)(void*, const char*, const char*);
typedef void (*fn_set_secondary_activate_target)(void*, GtkWidget*);
typedef void (*fn_set_title)(void*, const char*);

struct TrayData {
    SystemTray* self{nullptr};
    bool isAppIndicator{false};
    void* libHandle{nullptr};
    void* indicator{nullptr};
    GtkWidget* menu{nullptr};
    GtkStatusIcon* statusIcon{nullptr};
};

GtkWidget* buildTrayMenu(SystemTray* self) {
    GtkWidget* menu = gtk_menu_new();
    auto* i18n = sc::core::I18n::get();

    // 1. Capture now
    std::string capLabel = i18n->get("tray.capture");
    if (capLabel.empty() || capLabel == "tray.capture") capLabel = "截图";
    auto* capItem = gtk_menu_item_new_with_label(capLabel.c_str());
    auto* capCtx = new MenuActCtx{self, menu, -1};
    g_signal_connect_data(capItem, "activate", G_CALLBACK(onCaptureItem), capCtx,
                          freeMenuActCtx, (GConnectFlags)0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), capItem);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // 2. Delayed capture options
    auto addDelayItem = [self, menu, i18n](int seconds) {
        std::string label = i18n->get("tray.delayN");
        if (auto pos = label.find("{n}"); pos != std::string::npos)
            label.replace(pos, 3, std::to_string(seconds));
        auto* item = gtk_menu_item_new_with_label(label.c_str());
        auto* dctx = new MenuActCtx{self, menu, seconds};
        g_signal_connect_data(item, "activate", G_CALLBACK(onDelayedItem), dctx,
                              freeMenuActCtx, (GConnectFlags)0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    };
    addDelayItem(3);
    addDelayItem(5);
    addDelayItem(10);

    auto* custom = gtk_menu_item_new_with_label(i18n->get("tray.delayCustom").c_str());
    auto* customCtx = new MenuActCtx{self, menu, 0}; // 0 = open custom-delay dialog
    g_signal_connect_data(custom, "activate", G_CALLBACK(onDelayedItem), customCtx,
                          freeMenuActCtx, (GConnectFlags)0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), custom);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // Menu styling for GTK-rendered menu popups
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

    // 3. About
    auto* about = gtk_menu_item_new_with_label(i18n->get("tray.about").c_str());
    g_signal_connect_swapped(about, "activate", G_CALLBACK(+[](SystemTray* s) {
        if (s->onSettings) s->onSettings();
    }), self);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about);

    // 4. Exit
    auto* exit = gtk_menu_item_new_with_label(i18n->get("tray.exit").c_str());
    g_signal_connect_swapped(exit, "activate", G_CALLBACK(+[](SystemTray* s) {
        if (s->onExit) s->onExit();
    }), self);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), exit);

    gtk_widget_show_all(menu);
    return menu;
}

} // namespace

void SystemTray::init(const std::string& iconPath) {
    if (icon_) return;
    auto* data = new TrayData();
    data->self = this;

    // Build the persistent tray menu
    data->menu = buildTrayMenu(this);
    g_object_ref_sink(data->menu);

    // Prefer the legacy XEmbed icon when the panel really hosts a systray there:
    // left click then activates the app (capture) and right click opens the
    // menu, which libappindicator cannot offer (it exposes no primary-activate
    // callback). GNOME Shell owns no XEmbed selection, so it keeps using
    // appindicator below.
    const bool useXEmbedTray = xembedTrayHostAvailable();

    // Attempt to load libappindicator / libayatana-appindicator via dlopen.
    // Modern Linux desktop environments (Ubuntu GNOME 3.36+, Debian, Fedora, KDE)
    // rely on StatusNotifierItem over DBus, since GNOME dropped legacy XEmbed
    // system tray support (_NET_SYSTEM_TRAY) back in GNOME 3.26.
    const char* libs[] = {
        "libappindicator3.so.1",
        "libayatana-appindicator3.so.1",
        "libappindicator3.so",
        "libayatana-appindicator3.so"
    };
    void* handle = nullptr;
    if (!useXEmbedTray) {
        for (const char* lib : libs) {
            handle = dlopen(lib, RTLD_LAZY);
            if (handle) break;
        }
    }

    if (handle) {
        auto p_new_with_path = (fn_new_with_path)dlsym(handle, "app_indicator_new_with_path");
        auto p_new = (fn_new)dlsym(handle, "app_indicator_new");
        auto p_set_status = (fn_set_status)dlsym(handle, "app_indicator_set_status");
        auto p_set_menu = (fn_set_menu)dlsym(handle, "app_indicator_set_menu");
        auto p_set_icon_full = (fn_set_icon_full)dlsym(handle, "app_indicator_set_icon_full");
        auto p_set_secondary = (fn_set_secondary_activate_target)dlsym(handle, "app_indicator_set_secondary_activate_target");
        auto p_set_title = (fn_set_title)dlsym(handle, "app_indicator_set_title");

        if ((p_new_with_path || p_new) && p_set_status && p_set_menu) {
            std::filesystem::path p(iconPath);
            std::string dir = p.parent_path().string();
            std::string stem = p.stem().string();

            void* ind = nullptr;
            if (p_new_with_path) {
                ind = p_new_with_path("simplescreenshot", stem.c_str(), 0, dir.c_str());
            }
            if (!ind && p_new) {
                ind = p_new("simplescreenshot", iconPath.c_str(), 0);
            }

            if (ind) {
                if (p_set_icon_full) {
                    p_set_icon_full(ind, iconPath.c_str(), "SimpleScreenShot");
                }
                if (p_set_title) {
                    p_set_title(ind, "SimpleScreenShot");
                }

                // Wire secondary (middle-click) click on indicator to the first item ("Capture")
                GList* children = gtk_container_get_children(GTK_CONTAINER(data->menu));
                if (children) {
                    if (p_set_secondary) {
                        p_set_secondary(ind, GTK_WIDGET(children->data));
                    }
                    g_list_free(children);
                }

                p_set_menu(ind, GTK_MENU(data->menu));
                p_set_status(ind, 1); // APP_INDICATOR_STATUS_ACTIVE

                data->isAppIndicator = true;
                data->indicator = ind;
                data->libHandle = handle;
                icon_ = data;
                return;
            }
        }
        dlclose(handle);
    }

    // Fallback: Legacy GtkStatusIcon (XEmbed protocol).
    // Native on Linux Mint (Cinnamon), MATE, XFCE panels that still support _NET_SYSTEM_TRAY.
    GtkStatusIcon* sIcon = gtk_status_icon_new_from_file(iconPath.c_str());
    gtk_status_icon_set_tooltip_text(sIcon, "SimpleScreenShot");
    data->isAppIndicator = false;
    data->statusIcon = sIcon;
    icon_ = data;

    g_signal_connect(sIcon, "activate", G_CALLBACK(+[](GtkStatusIcon*, gpointer ptr) {
        auto* d = static_cast<TrayData*>(ptr);
        if (d && d->self && d->self->onActivate) d->self->onActivate();
    }), data);

    g_signal_connect(sIcon, "popup-menu", G_CALLBACK(+[](GtkStatusIcon*, guint, guint, gpointer ptr) {
        auto* d = static_cast<TrayData*>(ptr);
        if (d && d->menu) {
            gtk_widget_show_all(d->menu);
            gtk_menu_popup_at_pointer(GTK_MENU(d->menu), nullptr);
        }
    }), data);
}

void SystemTray::dispose() {
    if (!icon_) return;
    auto* data = static_cast<TrayData*>(icon_);
    if (data->isAppIndicator) {
        if (data->indicator) {
            g_object_unref(data->indicator);
            data->indicator = nullptr;
        }
        if (data->libHandle) {
            dlclose(data->libHandle);
            data->libHandle = nullptr;
        }
    } else {
        if (data->statusIcon) {
            g_object_unref(data->statusIcon);
            data->statusIcon = nullptr;
        }
    }
    if (data->menu) {
        gtk_widget_destroy(data->menu);
        g_object_unref(data->menu);
        data->menu = nullptr;
    }
    delete data;
    icon_ = nullptr;
}

} // namespace sc::platform
