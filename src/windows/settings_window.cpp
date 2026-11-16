#include "screencapture/windows/settings_window.h"
#include "screencapture/core/app_context.h"
#include "screencapture/core/config_manager.h"
#include "screencapture/core/i18n.h"
#include "screencapture/platform/x11_display.h"
#include <filesystem>

#ifndef SC_APP_VERSION
#define SC_APP_VERSION "dev"
#endif

namespace sc::windows {

std::unique_ptr<SettingsWindow> SettingsWindow::instance_;

void SettingsWindow::init() {
    if (instance_) return;
    auto* display = sc::platform::X11Display::get();
    auto monitors = display->monitors();
    if (monitors.empty()) monitors.push_back(display->virtualDesktop());
    const auto& m = monitors.front();

    instance_.reset(new SettingsWindow());
    constexpr float kW = 420.f, kH = 232.f;
    float left = static_cast<float>(m.left) + (static_cast<float>(m.width()) - kW) * 0.5f;
    float top = static_cast<float>(m.top) + (static_cast<float>(m.height()) - kH) * 0.5f;
    instance_->create("About", core::RectF::fromXYWH(left, top, kW, kH),
                      {sc::ui::WindowFlag::Topmost});
    instance_->grabKeyboardFocus();
}

SettingsWindow* SettingsWindow::get() { return instance_.get(); }
void SettingsWindow::dispose() { instance_.reset(); }

void SettingsWindow::onCreated() {
    auto* i18n = sc::core::I18n::get();
    auto* cfg = sc::core::ConfigManager::get();

    // Opaque light background: the RGBA visual would otherwise leave the body
    // unpainted (rendered black by the compositor), same as the launcher.
    body->setBg(0xFFFFFFFF);
    body->setBorder(1.f, 0xD9D9D9FF);

    auto* title = body->makeChild<sc::ui::Label>();
    title->setText(std::string("SimpleScreenShot ") + SC_APP_VERSION);
    title->setFontSize(16.f);
    title->setFontFamily("Sans Bold");
    title->setPosition(20.f, 14.f);
    title->setSize(340.f, 28.f);

    auto* closeBtn = body->makeChild<sc::ui::Button>();
    closeBtn->setText("\xc3\x97"); // U+00D7 close glyph
    closeBtn->setPosition(382.f, 10.f);
    closeBtn->setSize(30.f, 26.f);
    closeBtn->setJustifyContent(sc::ui::Justify::Center);
    closeBtn->setAlignItems(sc::ui::Align::Center);
    closeBtn->setFontSize(15.f);
    closeBtn->setColor(0x666666FF);
    closeBtn->setBorderRadius(6.f);
    closeBtn->hoverBgColor = 0xEDEDEDFF;
    closeBtn->onClick.add([](sc::ui::Button*) {
        sc::core::AppContext::get()->postTask([] { SettingsWindow::dispose(); });
    });

    auto* divider = body->makeChild<sc::ui::WidgetNode>();
    divider->setBg(0xE8E8E8FF);
    divider->setPosition(0.f, 50.f);
    divider->setSize(420.f, 1.f);

    const struct { const char* key; std::string value; } rows[] = {
        {"about.version", SC_APP_VERSION},
        {"about.language", cfg->getLang()},
        {"about.hotkey", cfg->getShortcutKey("cap")},
        {"about.author", "xinwf <xinwf18@gmail.com>"},
    };
    float y = 66.f;
    for (const auto& row : rows) {
        auto* label = body->makeChild<sc::ui::Label>();
        label->setText(i18n->get(row.key) + row.value);
        label->setFontSize(13.f);
        label->setPosition(20.f, y);
        label->setSize(380.f, 24.f);
        y += 28.f;
    }

    auto* tech = body->makeChild<sc::ui::Label>();
    tech->setText("GTK3 + Cairo + Pango + X11 — Pure Linux C++20");
    tech->setFontSize(11.f);
    tech->setColor(0x888888FF);
    tech->setPosition(20.f, y + 6.f);
    tech->setSize(380.f, 22.f);
}

void SettingsWindow::onKeyDown(uint32_t key) {
    constexpr uint32_t ESC = 0xff1b;
    if (key == ESC) {
        // Deferred so we don't destroy this window inside its own event handler.
        sc::core::AppContext::get()->postTask([] { SettingsWindow::dispose(); });
    }
}

} // namespace sc::windows
