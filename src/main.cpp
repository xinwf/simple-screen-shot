#include "screencapture/core/app_context.h"
#include "screencapture/core/config_manager.h"
#include "screencapture/core/i18n.h"
#include "screencapture/core/types.h"
#include "screencapture/gfx/text_layout.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/global_shortcut.h"
#include "screencapture/platform/system_tray.h"
#include "screencapture/platform/screen_capturer.h"
#include "screencapture/platform/dialogs.h"
#include "screencapture/platform/single_instance.h"
#include "screencapture/platform/x11_error_trap.h"
#include "screencapture/media/ocr_runner.h"
#include "screencapture/windows/capture_window.h"
#include "screencapture/windows/delay_launcher.h"
#include "screencapture/windows/pin_window.h"
#include "screencapture/windows/settings_window.h"
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <iostream>

using namespace sc;

static std::filesystem::path findAssetFile(const std::string& relName) {
    auto* cfg = core::ConfigManager::get();
    std::vector<std::filesystem::path> dirs = {
        cfg->exeDir() / "assets",
        cfg->exeDir().parent_path() / "assets",
        // System install layout: /usr/bin/screencapture + /usr/share/simplescreenshot/assets.
        cfg->exeDir().parent_path() / "share" / "simplescreenshot" / "assets",
        std::filesystem::current_path() / "assets",
        cfg->dataPath() / "assets"
    };
    for (const auto& d : dirs) {
        auto p = d / relName;
        if (std::filesystem::exists(p)) return p;
    }
    return cfg->exeDir() / "assets" / relName;
}

static void registerFont() {
    auto fontPath = findAssetFile("iconfont.ttf");
    if (std::filesystem::exists(fontPath)) {
        gfx::FontRegistry::get()->addFontFile(fontPath.string());
    }
}

// Probe RapidOCR support once at startup, BEFORE any window is created (the
// capture overlay is an override-redirect popup that would cover a dialog).
// When unsupported, explain that tesseract will be used instead; the app only
// continues (tray, capture UI) after the dialog is dismissed. The probe costs
// ~0.15s, so a synchronous call keeps this simple and race-free.
static void checkOcrEngine() {
    auto* cfg = core::ConfigManager::get();
    if (cfg->getOcrRapidocrHintHidden()) return;
    if (!cfg->getOcrPlugin().empty()) return; // custom plugin: assume the user knows best

    auto bundled = findAssetFile("plugins/ocr_rapidocr.py");
    if (!std::filesystem::exists(bundled)) return;
    if (media::OcrRunner::rapidocrAvailable(bundled.string())) return; // RapidOCR usable

    auto* i18n = core::I18n::get();
    bool hide = platform::showInfoDialogWithCheckbox(
        i18n->get("cap.ocrHintTitle"),
        i18n->get("cap.ocrHintBody"),
        i18n->get("cap.dontRemind"));
    if (hide) cfg->setOcrRapidocrHintHidden(true);

    // Drain everything the dialog left queued (unmap/destroy, repaints), then
    // give the compositor ~2 frames to actually repaint the root window — the
    // capture UI's pre-capture reads composited root pixels, and without the
    // settle time the just-closed dialog would appear as a ghost in it.
    // Measured ~100ms on this GNOME/X11 desktop; 200ms for margin.
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
    g_usleep(200 * 1000);
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
    if (GdkDisplay* d = gdk_display_get_default()) gdk_display_flush(d);
}

static void setupTray() {
    auto* tray = platform::SystemTray::get();
    auto icon = findAssetFile("logo.png");
    tray->init(icon.string());
    tray->onActivate = []() {
        if (!windows::CaptureWindow::hasWindow()) windows::CaptureWindow::init();
    };
    tray->onSettings = []() { windows::SettingsWindow::init(); };
    tray->onDelayedCapture = [](int seconds) {
        windows::DelayLauncherWindow::startTrayCountdown(seconds);
    };
    tray->onExit = []() { core::AppContext::get()->quit(0); };
}

int main(int argc, char** argv) {
    // Single instance: a second launch signals the primary to re-show its
    // delay dialog and exits quietly instead of creating another tray icon.
    if (sc::platform::anotherInstanceRunning()) return 0;

    // Lock GTK3 to the X11 backend for deterministic coordinates + XShape control.
    setenv("GDK_BACKEND", "x11", 1);
    gtk_init(&argc, &argv);
    // Keep one error trap outstanding for the whole app lifetime: the RGBA
    // overlay windows get unmapped / reshaped / hidden rapidly, and an async
    // BadMatch arriving after a trap region has popped would otherwise hit
    // GDK's default handler, which aborts the entire process. With a trap
    // outstanding, transient X errors are recorded and discarded instead.
    sc::platform::x11ErrorTrapPush();
    sc::platform::publishInstancePid();
    // SIGUSR1 from a second launch: re-show the delay dialog on the main loop.
    g_unix_signal_add(SIGUSR1, +[](gpointer) -> int {
        sc::core::AppContext::get()->postTask([] {
            windows::DelayLauncherWindow::init();
        });
        return G_SOURCE_CONTINUE;   // stay registered for further launches
    }, nullptr);

    auto* app = core::AppContext::get();
    app->init(argc, argv);
    core::ConfigManager::get()->init();
    core::I18n::get()->init();
    platform::X11Display::get()->init();
    registerFont();
    checkOcrEngine();

    // Global capture hotkey.
    auto* shortcuts = platform::GlobalShortcut::get();
    shortcuts->init();
    shortcuts->registerHotkey(core::ConfigManager::get()->getShortcutKey("cap"), []() {
        if (!windows::CaptureWindow::hasWindow()) windows::CaptureWindow::init();
    });

    const bool autoQuit = app->autoQuit();
    const std::string enter = app->enterMode();

    if (autoQuit) {
        windows::CaptureWindow::init();
    } else if (enter == "tray") {
        setupTray();
    } else {
        // Normal launch: show the delayed-capture dialog first, keep tray alive.
        setupTray();
        windows::DelayLauncherWindow::init();
    }

    app->onBeforeQuit.add([]() {
        platform::GlobalShortcut::get()->unregisterAll();
        platform::clearInstancePid();
        platform::SystemTray::get()->dispose();
        windows::CaptureWindow::dispose();
        windows::PinWindow::dispose();
        windows::SettingsWindow::dispose();
    });

    app->run();
    return app->exitCode();
}
