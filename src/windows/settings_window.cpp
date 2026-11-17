#include "screencapture/windows/settings_window.h"
#include "screencapture/windows/capture_window.h"
#include "screencapture/core/app_context.h"
#include "screencapture/core/config_manager.h"
#include "screencapture/core/i18n.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/global_shortcut.h"
#include "screencapture/platform/file_dialog.h"
#include <gdk/gdkx.h>
#include <filesystem>

#ifndef SC_APP_VERSION
#define SC_APP_VERSION "dev"
#endif

namespace sc::windows {

std::unique_ptr<SettingsWindow> SettingsWindow::instance_;

void SettingsWindow::init() {
    if (instance_) {
        instance_->present();
        return;
    }
    auto* display = sc::platform::X11Display::get();
    auto monitors = display->monitors();
    if (monitors.empty()) monitors.push_back(display->virtualDesktop());
    const auto& m = monitors.front();

    instance_.reset(new SettingsWindow());
    constexpr float kW = 460.f, kH = 340.f;
    float left = static_cast<float>(m.left) + (static_cast<float>(m.width()) - kW) * 0.5f;
    float top = static_cast<float>(m.top) + (static_cast<float>(m.height()) - kH) * 0.5f;
    // Standard toplevel window (NOT override-redirect Topmost) so it does not
    // aggressively cover all other applications, and properly yields focus.
    instance_->create("Settings", core::RectF::fromXYWH(left, top, kW, kH), {});
    instance_->connectWindowSideEvents();
    instance_->present();
}

void SettingsWindow::present() {
    if (!native()) return;
    uint32_t ts = gtk_get_current_event_time();
    GdkWindow* gw = gtk_widget_get_window(native());
    if (ts == 0 && gw) {
        ts = gdk_x11_get_server_time(gw);
    }
    if (gw) {
        gdk_x11_window_set_user_time(gw, ts);
    }
    gtk_window_present_with_time(GTK_WINDOW(native()), ts);

    Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    if (dpy && gw) {
        Window xw = GDK_WINDOW_XID(gw);
        XEvent xev;
        std::memset(&xev, 0, sizeof(xev));
        xev.type = ClientMessage;
        xev.xclient.window = xw;
        xev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 2; // Pager / tray direct user action: bypass focus stealing prevention
        xev.xclient.data.l[1] = ts;
        xev.xclient.data.l[2] = 0;
        XSendEvent(dpy, DefaultRootWindow(dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
        XRaiseWindow(dpy, xw);
        XSetInputFocus(dpy, xw, RevertToPointerRoot, ts ? ts : CurrentTime);
        XFlush(dpy);
    }
    grabKeyboardFocus();
}

SettingsWindow* SettingsWindow::get() { return instance_.get(); }
void SettingsWindow::dispose() {
    if (instance_) {
        instance_->applyDirChanges();
        instance_.reset();
    }
}

sc::ui::TextBox* SettingsWindow::focusedField() const {
    if (hotkeyBox_ && hotkeyBox_->isFocused) return hotkeyBox_;
    if (dirBox_ && dirBox_->isFocused) return dirBox_;
    return nullptr;
}

void SettingsWindow::focusField(sc::ui::TextBox* box) {
    if (!box) return;
    if (hotkeyBox_ && hotkeyBox_ != box) hotkeyBox_->blur();
    if (dirBox_ && dirBox_ != box) dirBox_->blur();
    box->focus();
}

bool SettingsWindow::applyDirChanges() {
    if (!dirBox_) return false;
    auto* cfg = sc::core::ConfigManager::get();
    std::string dir = dirBox_->getText();
    while (!dir.empty() && (dir.back() == ' ' || dir.back() == '\t' || dir.back() == '/')) dir.pop_back();
    size_t start = dir.find_first_not_of(" \t");
    dir = (start == std::string::npos) ? std::string() : dir.substr(start);
    if (dir.empty()) {
        dirBox_->setText(cfg->getSaveDir());
        return false;
    }
    if (dir == "~") dir = g_get_home_dir();
    else if (dir.rfind("~/", 0) == 0) dir = std::string(g_get_home_dir()) + dir.substr(1);
    std::filesystem::path dirPath(dir);
    if (dirPath.is_relative()) dirPath = std::filesystem::current_path() / dirPath;
    std::error_code ec;
    if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath, ec);
        if (ec) {
            setStatusMsg("创建目录失败: " + ec.message(), 0xFF4D4FFF);
            return false;
        }
    } else if (!std::filesystem::is_directory(dirPath)) {
        setStatusMsg("目标路径不是有效目录", 0xFF4D4FFF);
        return false;
    }
    std::string norm = dirPath.string();
    if (norm != cfg->getSaveDir()) {
        cfg->setSaveDir(norm);
        cfg->save();
        dirBox_->setText(norm);
        setStatusMsg("保存路径已更新", 0x52C41AFF);
    }
    return true;
}

void SettingsWindow::onCreated() {
    rebuildUI();
}

void SettingsWindow::setStatusMsg(const std::string& msg, uint32_t color) {
    if (statusLabel_) {
        statusLabel_->setText(msg);
        statusLabel_->setColor(color);
        refresh();
    }
}

void SettingsWindow::rebuildUI() {
    resetHoverTracking();
    body->removeAllChildren();
    hotkeyBox_ = nullptr;
    hotkeyField_ = nullptr;
    dirBox_ = nullptr;
    dirField_ = nullptr;
    statusLabel_ = nullptr;
    pressBox_ = nullptr;
    fieldPressing_ = false;

    auto* i18n = sc::core::I18n::get();
    auto* cfg = sc::core::ConfigManager::get();

    body->setBg(0xFFFFFFFF);
    body->setBorder(1.f, 0xD9D9D9FF);

    // Title
    auto* title = body->makeChild<sc::ui::Label>();
    std::string titleStr = i18n->get("setting.title");
    if (titleStr.empty() || titleStr == "setting.title") titleStr = "ScreenCapture Settings";
    title->setText(titleStr);
    title->setFontSize(15.f);
    title->setFontFamily("Sans Bold");
    title->setPosition(20.f, 12.f);
    title->setSize(340.f, 26.f);

    // Close button
    auto* closeBtn = body->makeChild<sc::ui::Button>();
    closeBtn->setText("\xc3\x97"); // U+00D7 close glyph
    closeBtn->setPosition(420.f, 10.f);
    closeBtn->setSize(30.f, 26.f);
    closeBtn->setJustifyContent(sc::ui::Justify::Center);
    closeBtn->setAlignItems(sc::ui::Align::Center);
    closeBtn->setFontSize(16.f);
    closeBtn->setColor(0x666666FF);
    closeBtn->setBorderRadius(6.f);
    closeBtn->hoverBgColor = 0xEDEDEDFF;
    closeBtn->onClick.add([this](sc::ui::Button*) {
        applyDirChanges();
        sc::core::AppContext::get()->postTask([] { SettingsWindow::dispose(); });
    });

    // Divider
    auto* divider = body->makeChild<sc::ui::WidgetNode>();
    divider->setBg(0xE8E8E8FF);
    divider->setPosition(0.f, 44.f);
    divider->setSize(460.f, 1.f);

    // Row 1: Shortcut hotkey
    float y1 = 58.f;
    auto* hkLabel = body->makeChild<sc::ui::Label>();
    hkLabel->setText(i18n->get("about.hotkey"));
    hkLabel->setFontSize(13.f);
    hkLabel->setPosition(20.f, y1 + 4.f);
    hkLabel->setSize(110.f, 24.f);

    hotkeyField_ = body->makeChild<sc::ui::WidgetNode>();
    hotkeyField_->setPosition(130.f, y1);
    hotkeyField_->setSize(150.f, 28.f);
    hotkeyField_->setBg(0xFAFAFAFF);
    hotkeyField_->setBorder(1.f, 0xCCCCCCFF);
    hotkeyField_->setBorderRadius(4.f);

    hotkeyBox_ = hotkeyField_->makeChild<sc::ui::TextBox>();
    hotkeyBox_->setPosition(6.f, 4.f);
    hotkeyBox_->setSize(138.f, 20.f);
    hotkeyBox_->setFontSize(13.f);
    hotkeyBox_->setColor(0x222222FF);
    hotkeyBox_->setCaretColor(0x1677FFFF);
    hotkeyBox_->setText(cfg->getShortcutKey("cap"));

    // Apply hotkey button
    auto* applyHkBtn = body->makeChild<sc::ui::Button>();
    applyHkBtn->setText(i18n->get("save.save"));
    applyHkBtn->setFontSize(12.f);
    applyHkBtn->setPosition(290.f, y1);
    applyHkBtn->setSize(56.f, 28.f);
    applyHkBtn->setJustifyContent(sc::ui::Justify::Center);
    applyHkBtn->setAlignItems(sc::ui::Align::Center);
    applyHkBtn->setBg(0x1677FFFF);
    applyHkBtn->setColor(0xFFFFFFFF);
    applyHkBtn->setBorderRadius(4.f);
    applyHkBtn->hoverBgColor = 0x4096FFFF;
    applyHkBtn->onClick.add([this](sc::ui::Button*) {
        if (!hotkeyBox_) return;
        auto* cfg = sc::core::ConfigManager::get();
        std::string newKey = hotkeyBox_->getText();
        while (!newKey.empty() && (newKey.back() == ' ' || newKey.back() == '\t')) newKey.pop_back();
        size_t start = newKey.find_first_not_of(" \t");
        newKey = (start == std::string::npos) ? std::string() : newKey.substr(start);
        if (newKey.empty()) newKey = "Ctrl+Alt+A";

        auto* shortcuts = sc::platform::GlobalShortcut::get();
        shortcuts->unregisterAll();
        bool ok = shortcuts->registerHotkey(newKey, []() {
            if (!CaptureWindow::hasWindow()) CaptureWindow::init();
        });
        if (ok) {
            cfg->setShortcutKey("cap", newKey);
            cfg->save();
            setStatusMsg("快捷键已更新并生效！", 0x52C41AFF);
        } else {
            // Restore previous
            shortcuts->registerHotkey(cfg->getShortcutKey("cap"), []() {
                if (!CaptureWindow::hasWindow()) CaptureWindow::init();
            });
            hotkeyBox_->setText(cfg->getShortcutKey("cap"));
            setStatusMsg("快捷键注册失败（格式错误或冲突）", 0xFF4D4FFF);
        }
    });

    // Reset hotkey button
    auto* resetHkBtn = body->makeChild<sc::ui::Button>();
    resetHkBtn->setText("重置");
    resetHkBtn->setFontSize(12.f);
    resetHkBtn->setPosition(354.f, y1);
    resetHkBtn->setSize(50.f, 28.f);
    resetHkBtn->setJustifyContent(sc::ui::Justify::Center);
    resetHkBtn->setAlignItems(sc::ui::Align::Center);
    resetHkBtn->setBg(0xF0F0F0FF);
    resetHkBtn->setColor(0x555555FF);
    resetHkBtn->setBorder(1.f, 0xD9D9D9FF);
    resetHkBtn->setBorderRadius(4.f);
    resetHkBtn->hoverBgColor = 0xE0E0E0FF;
    resetHkBtn->onClick.add([this](sc::ui::Button*) {
        auto* cfg = sc::core::ConfigManager::get();
        std::string defKey = "Ctrl+Alt+A";
        auto* shortcuts = sc::platform::GlobalShortcut::get();
        shortcuts->unregisterAll();
        shortcuts->registerHotkey(defKey, []() {
            if (!CaptureWindow::hasWindow()) CaptureWindow::init();
        });
        cfg->setShortcutKey("cap", defKey);
        cfg->save();
        if (hotkeyBox_) hotkeyBox_->setText(defKey);
        setStatusMsg("已恢复默认快捷键 Ctrl+Alt+A", 0x52C41AFF);
    });

    // Row 2: Default Save Directory
    float y2 = 98.f;
    auto* dirTitle = body->makeChild<sc::ui::Label>();
    dirTitle->setText(i18n->get("save.folder"));
    dirTitle->setFontSize(13.f);
    dirTitle->setPosition(20.f, y2 + 4.f);
    dirTitle->setSize(110.f, 24.f);

    dirField_ = body->makeChild<sc::ui::WidgetNode>();
    dirField_->setPosition(130.f, y2);
    dirField_->setSize(216.f, 28.f);
    dirField_->setBg(0xFAFAFAFF);
    dirField_->setBorder(1.f, 0xCCCCCCFF);
    dirField_->setBorderRadius(4.f);

    dirBox_ = dirField_->makeChild<sc::ui::TextBox>();
    dirBox_->setPosition(6.f, 4.f);
    dirBox_->setSize(204.f, 20.f);
    dirBox_->setFontSize(12.f);
    dirBox_->setColor(0x222222FF);
    dirBox_->setCaretColor(0x1677FFFF);
    dirBox_->setText(cfg->getSaveDir());

    auto* browseBtn = body->makeChild<sc::ui::Button>();
    browseBtn->setText(i18n->get("save.browse"));
    browseBtn->setFontSize(12.f);
    browseBtn->setPosition(354.f, y2);
    browseBtn->setSize(70.f, 28.f);
    browseBtn->setJustifyContent(sc::ui::Justify::Center);
    browseBtn->setAlignItems(sc::ui::Align::Center);
    browseBtn->setBg(0xF0F0F0FF);
    browseBtn->setColor(0x333333FF);
    browseBtn->setBorder(1.f, 0xD9D9D9FF);
    browseBtn->setBorderRadius(4.f);
    browseBtn->hoverBgColor = 0xE0E0E0FF;
    browseBtn->onClick.add([this](sc::ui::Button*) {
        auto* cfg = sc::core::ConfigManager::get();
        std::string cur = dirBox_ ? dirBox_->getText() : cfg->getSaveDir();
        std::string chosen = sc::platform::FileDialog::chooseDirectory(cur, nullptr);
        if (!chosen.empty()) {
            cfg->setSaveDir(chosen);
            cfg->save();
            if (dirBox_) dirBox_->setText(chosen);
            setStatusMsg("保存路径已更新", 0x52C41AFF);
        }
    });

    // Row 3: UI Language
    float y3 = 138.f;
    auto* langLabel = body->makeChild<sc::ui::Label>();
    langLabel->setText(i18n->get("setting.language"));
    langLabel->setFontSize(13.f);
    langLabel->setPosition(20.f, y3 + 4.f);
    langLabel->setSize(110.f, 24.f);

    std::string currentLang = cfg->getLang();

    // Chinese button
    auto* zhBtn = body->makeChild<sc::ui::Button>();
    zhBtn->setText("中文 (简体)");
    zhBtn->setFontSize(12.f);
    zhBtn->setPosition(130.f, y3);
    zhBtn->setSize(96.f, 28.f);
    zhBtn->setJustifyContent(sc::ui::Justify::Center);
    zhBtn->setAlignItems(sc::ui::Align::Center);
    zhBtn->setBorderRadius(4.f);
    if (currentLang == "zh-CN") {
        zhBtn->setBg(0x1677FFFF);
        zhBtn->setColor(0xFFFFFFFF);
    } else {
        zhBtn->setBg(0xF0F0F0FF);
        zhBtn->setColor(0x555555FF);
        zhBtn->setBorder(1.f, 0xD9D9D9FF);
    }
    zhBtn->onClick.add([this](sc::ui::Button*) {
        applyDirChanges();
        auto* cfg = sc::core::ConfigManager::get();
        if (cfg->getLang() == "zh-CN") return;
        cfg->setLang("zh-CN");
        cfg->save();
        sc::core::I18n::get()->setLanguage("zh-CN");
        rebuildUI();
        setStatusMsg("语言已切换为中文 (简体)", 0x52C41AFF);
    });

    // English button
    auto* enBtn = body->makeChild<sc::ui::Button>();
    enBtn->setText("English");
    enBtn->setFontSize(12.f);
    enBtn->setPosition(234.f, y3);
    enBtn->setSize(88.f, 28.f);
    enBtn->setJustifyContent(sc::ui::Justify::Center);
    enBtn->setAlignItems(sc::ui::Align::Center);
    enBtn->setBorderRadius(4.f);
    if (currentLang == "en-US") {
        enBtn->setBg(0x1677FFFF);
        enBtn->setColor(0xFFFFFFFF);
    } else {
        enBtn->setBg(0xF0F0F0FF);
        enBtn->setColor(0x555555FF);
        enBtn->setBorder(1.f, 0xD9D9D9FF);
    }
    enBtn->onClick.add([this](sc::ui::Button*) {
        applyDirChanges();
        auto* cfg = sc::core::ConfigManager::get();
        if (cfg->getLang() == "en-US") return;
        cfg->setLang("en-US");
        cfg->save();
        sc::core::I18n::get()->setLanguage("en-US");
        rebuildUI();
        setStatusMsg("Language changed to English", 0x52C41AFF);
    });

    // Status Message Label
    statusLabel_ = body->makeChild<sc::ui::Label>();
    statusLabel_->setText("");
    statusLabel_->setFontSize(12.f);
    statusLabel_->setPosition(20.f, 178.f);
    statusLabel_->setSize(420.f, 22.f);
    statusLabel_->setColor(0x52C41AFF);

    // Divider
    auto* divider2 = body->makeChild<sc::ui::WidgetNode>();
    divider2->setBg(0xE8E8E8FF);
    divider2->setPosition(20.f, 206.f);
    divider2->setSize(420.f, 1.f);

    // Row 4: About & author info
    float y4 = 216.f;
    auto* verLabel = body->makeChild<sc::ui::Label>();
    verLabel->setText(i18n->get("about.version") + " " + SC_APP_VERSION + " (Linux x86_64)");
    verLabel->setFontSize(12.f);
    verLabel->setColor(0x555555FF);
    verLabel->setPosition(20.f, y4);
    verLabel->setSize(420.f, 20.f);

    auto* authorLabel = body->makeChild<sc::ui::Label>();
    authorLabel->setText(i18n->get("about.author") + " " + "xinwf <xinwf18@gmail.com>");
    authorLabel->setFontSize(12.f);
    authorLabel->setColor(0x777777FF);
    authorLabel->setPosition(20.f, y4 + 22.f);
    authorLabel->setSize(420.f, 20.f);

    auto* tech = body->makeChild<sc::ui::Label>();
    tech->setText("GTK3 + Cairo + Pango + X11 · PulseAudio / PipeWire");
    tech->setFontSize(11.f);
    tech->setColor(0x999999FF);
    tech->setPosition(20.f, y4 + 44.f);
    tech->setSize(420.f, 20.f);

    layout();
}

void SettingsWindow::onMouseDown(const core::PointF& pos, bool right) {
    if (right) return;
    grabKeyboardFocus();
    auto* hit = body->hitTest(pos.x, pos.y);

    // Title drag
    if (pos.y <= 44.f && hit && !dynamic_cast<sc::ui::Button*>(hit)) {
        dragging_ = true;
        winX0_ = x;
        winY0_ = y;
        absPressX_ = x + pos.x;
        absPressY_ = y + pos.y;
        if (GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default())) {
            gdk_seat_grab(seat, gtk_widget_get_window(native()),
                          GDK_SEAT_CAPABILITY_POINTER, FALSE, nullptr, nullptr,
                          nullptr, nullptr);
        }
        return;
    }

    if (hotkeyBox_ && (hit == hotkeyBox_ || hit == hotkeyField_)) {
        focusField(hotkeyBox_);
        hotkeyBox_->setCaretByte(hotkeyBox_->caretIndexFromX(pos.x, dpi));
        fieldPressing_ = true;
        pressBox_ = hotkeyBox_;
        refresh();
    } else if (dirBox_ && (hit == dirBox_ || hit == dirField_)) {
        focusField(dirBox_);
        dirBox_->setCaretByte(dirBox_->caretIndexFromX(pos.x, dpi));
        fieldPressing_ = true;
        pressBox_ = dirBox_;
        refresh();
    } else if (!dynamic_cast<sc::ui::Button*>(hit)) {
        if (dirBox_ && dirBox_->isFocused) applyDirChanges();
        if (hotkeyBox_) hotkeyBox_->blur();
        if (dirBox_) dirBox_->blur();
        refresh();
    }
}

void SettingsWindow::onMouseMove(const core::PointF& pos) {
    if (dragging_) {
        int nx = winX0_ + static_cast<int>(x + pos.x - absPressX_);
        int ny = winY0_ + static_cast<int>(y + pos.y - absPressY_);
        x = nx;
        y = ny;
        if (native()) {
            gtk_window_move(GTK_WINDOW(native()), nx, ny);
            if (GdkWindow* gw = gtk_widget_get_window(native())) gdk_window_move(gw, nx, ny);
        }
        return;
    }
    if (fieldPressing_ && pressBox_ && pressBox_->isFocused) {
        pressBox_->setCaretExtend(pressBox_->caretIndexFromX(pos.x, dpi));
        refresh();
    }
}

void SettingsWindow::onMouseUp(const core::PointF&, bool) {
    if (dragging_) {
        dragging_ = false;
        if (GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default())) {
            gdk_seat_ungrab(seat);
        }
    }
    fieldPressing_ = false;
    pressBox_ = nullptr;
}

void SettingsWindow::onKeyDown(uint32_t key) {
    constexpr uint32_t ESC = 0xff1b;
    constexpr uint32_t ENTER = 0xff0d;
    constexpr uint32_t KP_ENTER = 0xff8d;
    constexpr uint32_t TAB = 0xff09;
    if (key == ESC) {
        applyDirChanges();
        sc::core::AppContext::get()->postTask([] { SettingsWindow::dispose(); });
        return;
    }

    if (key == TAB) {
        if (hotkeyBox_ && hotkeyBox_->isFocused) focusField(dirBox_);
        else if (dirBox_ && dirBox_->isFocused) focusField(hotkeyBox_);
        else if (hotkeyBox_) focusField(hotkeyBox_);
        refresh();
        return;
    }

    if (key == ENTER || key == KP_ENTER) {
        if (dirBox_ && dirBox_->isFocused) {
            applyDirChanges();
            refresh();
            return;
        }
        if (hotkeyBox_ && hotkeyBox_->isFocused) {
            // Apply current text
            std::string newKey = hotkeyBox_->getText();
            while (!newKey.empty() && (newKey.back() == ' ' || newKey.back() == '\t')) newKey.pop_back();
            size_t start = newKey.find_first_not_of(" \t");
            newKey = (start == std::string::npos) ? std::string() : newKey.substr(start);
            if (newKey.empty()) newKey = "Ctrl+Alt+A";

            auto* shortcuts = sc::platform::GlobalShortcut::get();
            shortcuts->unregisterAll();
            bool ok = shortcuts->registerHotkey(newKey, []() {
                if (!CaptureWindow::hasWindow()) CaptureWindow::init();
            });
            if (ok) {
                auto* cfg = sc::core::ConfigManager::get();
                cfg->setShortcutKey("cap", newKey);
                cfg->save();
                setStatusMsg("快捷键已更新并生效！", 0x52C41AFF);
            } else {
                auto* cfg = sc::core::ConfigManager::get();
                shortcuts->registerHotkey(cfg->getShortcutKey("cap"), []() {
                    if (!CaptureWindow::hasWindow()) CaptureWindow::init();
                });
                hotkeyBox_->setText(cfg->getShortcutKey("cap"));
                setStatusMsg("快捷键注册失败（格式错误或冲突）", 0xFF4D4FFF);
            }
            return;
        }
    }

    if (sc::ui::TextBox* box = focusedField()) {
        constexpr uint32_t BACKSPACE_K = 0xff08;
        constexpr uint32_t DELETE_K = 0xffff;
        constexpr uint32_t LEFT_K = 0xff51;
        constexpr uint32_t RIGHT_K = 0xff53;
        constexpr uint32_t HOME_K = 0xff50;
        constexpr uint32_t END_K = 0xff57;
        bool shift = shiftHeld();
        bool handled = false;
        switch (key) {
            case BACKSPACE_K: handled = box->backspace(); break;
            case DELETE_K: handled = box->deleteChar(); break;
            case LEFT_K: handled = box->caretMove(-1, shift); break;
            case RIGHT_K: handled = box->caretMove(1, shift); break;
            case HOME_K: box->caretToStart(shift); handled = true; break;
            case END_K: box->caretToEnd(shift); handled = true; break;
            default: break;
        }
        if (handled) refresh();
    }
}

void SettingsWindow::onTextInput(const std::string& utf8) {
    if (auto* box = focusedField()) {
        box->insertAtCaret(utf8);
        refresh();
    }
}

} // namespace sc::windows
