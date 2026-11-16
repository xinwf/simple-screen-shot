#include "screencapture/windows/save_dialog.h"
#include "screencapture/core/app_context.h"
#include "screencapture/core/i18n.h"
#include "screencapture/platform/clipboard_service.h"
#include "screencapture/platform/file_dialog.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/windows/capture_window.h"
#include "screencapture/windows/pin_window.h"
#include <glib.h>
#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <functional>

namespace sc::windows {

namespace {

constexpr float kW = 380.f;
constexpr float kPreviewMaxW = 340.f, kPreviewMaxH = 240.f;
constexpr uint32_t kGreen = 0x30A46CFF;
constexpr uint32_t kGreenHover = 0x2A925FFF;
constexpr uint32_t kBorder = 0xC9C9C9FF;

std::string tr(const std::string& key) { return sc::core::I18n::get()->get(key); }

std::string defaultName() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf) + " " + tr("save.screenshotName") + ".png";
}

} // namespace

std::unique_ptr<SaveDialogWindow> SaveDialogWindow::instance_;

void SaveDialogWindow::open(std::shared_ptr<gfx::ImageSurface> image, bool quitWhenDone) {
    if (instance_) instance_.reset();
    if (!image || !image->valid()) return;

    auto* display = sc::platform::X11Display::get();
    auto monitors = display->monitors();
    if (monitors.empty()) monitors.push_back(display->virtualDesktop());
    const auto& m = monitors.front();

    instance_.reset(new SaveDialogWindow());
    instance_->image_ = std::move(image);
    instance_->quitWhenDone_ = quitWhenDone;
    const gchar* pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    instance_->dir_ = pics && *pics ? std::filesystem::path(pics)
                                    : std::filesystem::path(g_get_home_dir());

    // Preview box follows the image aspect (bounded) so the shot fills it
    // completely instead of floating on a letterboxed canvas.
    float iw = static_cast<float>(instance_->image_->width());
    float ih = static_cast<float>(instance_->image_->height());
    float scale = std::min(kPreviewMaxW / iw, kPreviewMaxH / ih);
    instance_->previewW_ = std::max(iw * scale, 160.f);
    instance_->previewH_ = std::max(ih * scale, 90.f);
    const float kH = 52.f + instance_->previewH_ + 12.f + 28.f + 8.f + 28.f + 14.f;

    float left = static_cast<float>(m.left) + (static_cast<float>(m.width()) - kW) * 0.5f;
    float top = static_cast<float>(m.top) + (static_cast<float>(m.height()) - kH) * 0.5f;
    instance_->create("Save Screenshot", core::RectF::fromXYWH(left, top, kW, kH),
                      {sc::ui::WindowFlag::Topmost});
    // While the pointer grab is active (window drag), events arrive on the
    // toplevel window instead of the drawing area.
    instance_->connectWindowSideEvents();
    instance_->grabKeyboardFocus();
}

void SaveDialogWindow::dispose() { instance_.reset(); }

void SaveDialogWindow::onCreated() {
    const float nameY = 52.f + previewH_ + 10.f;
    const float folderY = nameY + 38.f;

    body->setBg(0xFFFFFFFF);
    body->setBorder(1.f, 0xD9D9D9FF);

    // Top action bar: Cancel | Copy to clipboard | Save, like the reference.
    auto makeBar = [this](float x, float w, const std::string& text, bool green,
                          std::function<void()> onClick) {
        auto* btn = body->makeChild<sc::ui::Button>();
        btn->setText(text);
        btn->setPosition(x, 6.f);
        btn->setSize(w, 28.f);
        btn->setJustifyContent(sc::ui::Justify::Center);
        btn->setAlignItems(sc::ui::Align::Center);
        btn->setFontSize(13.f);
        btn->setBorderRadius(6.f);
        if (green) {
            btn->setBg(kGreen);
            btn->hoverBgColor = kGreenHover;
            btn->setColor(0xFFFFFFFF);
        } else {
            btn->setBg(0xFFFFFFFF);
            btn->setBorder(1.f, kBorder);
            btn->hoverBgColor = 0xF2F2F2FF;
        }
        btn->onClick.add([onClick](sc::ui::Button*) { onClick(); });
        return btn;
    };
    makeBar(8.f, 86.f, tr("save.cancel"), false, [this] { finish(false); });
    makeBar(kW - 190.f, 102.f, tr("save.copy"), false, [this] {
        sc::platform::ClipboardService::setImage(image_);
        finish(false);
    });
    makeBar(kW - 82.f, 74.f, tr("save.save"), true, [this] { finish(true); });

    auto* divider = body->makeChild<sc::ui::WidgetNode>();
    divider->setBg(0xE8E8E8FF);
    divider->setPosition(0.f, 40.f);
    divider->setSize(kW, 1.f);

    // Image preview: the box matches the image aspect (see open()), so the
    // shot fills it edge to edge. The dark base only shows for pathological
    // aspect ratios; CanvasNode paints the node bg AFTER onPaint, so the
    // base must be drawn inside the handler.
    preview_ = body->makeChild<sc::ui::CanvasNode>();
    preview_->setPosition(20.f, 52.f);
    preview_->setSize(previewW_, previewH_);
    preview_->onPaint.add([this](gfx::CanvasContext& ctx) {
        const float x = preview_->x, y = preview_->y;
        const float w = preview_->width, h = preview_->height;
        ctx.fillRoundedRect(core::RectF::fromXYWH(x, y, w, h), 4.f,
                            core::ColorRgba::fromHex(0x1E1E1E));
        if (!image_ || !image_->valid()) return;
        ctx.drawImage(*image_, core::RectF::fromXYWH(x, y, w, h), 1.f, true);
    });

    // Name row.
    auto* nameLabel = body->makeChild<sc::ui::Label>();
    nameLabel->setText(tr("save.name"));
    nameLabel->setFontSize(13.f);
    nameLabel->setPosition(20.f, nameY);
    nameLabel->setSize(92.f, 28.f);
    nameLabel->setAlignItems(sc::ui::Align::Center);

    nameField_ = body->makeChild<sc::ui::WidgetNode>();
    nameField_->setPosition(118.f, nameY);
    nameField_->setSize(242.f, 28.f);
    nameField_->setBg(0xFFFFFFFF);
    nameField_->setBorder(1.f, kBorder);
    nameField_->setBorderRadius(4.f);

    nameBox_ = nameField_->makeChild<sc::ui::TextBox>();
    nameBox_->setPosition(6.f, 2.f);
    nameBox_->setText(defaultName());
    nameBox_->setFontSize(13.f);
    nameBox_->setColor(0x222222FF);
    nameBox_->setCaretColor(0x30A46CFF);
    nameBox_->focus();

    // Folder row: editable path text box + native picker button.
    auto* dirLabelTitle = body->makeChild<sc::ui::Label>();
    dirLabelTitle->setText(tr("save.folder"));
    dirLabelTitle->setFontSize(13.f);
    dirLabelTitle->setPosition(20.f, folderY);
    dirLabelTitle->setSize(92.f, 28.f);
    dirLabelTitle->setAlignItems(sc::ui::Align::Center);

    dirField_ = body->makeChild<sc::ui::WidgetNode>();
    dirField_->setPosition(118.f, folderY);
    dirField_->setSize(184.f, 28.f);
    dirField_->setBg(0xFFFFFFFF);
    dirField_->setBorder(1.f, kBorder);
    dirField_->setBorderRadius(4.f);

    dirBox_ = dirField_->makeChild<sc::ui::TextBox>();
    dirBox_->setPosition(4.f, 2.f);
    dirBox_->setText(dir_.string());
    dirBox_->setFontSize(12.f);
    dirBox_->setColor(0x222222FF);
    dirBox_->setCaretColor(0x30A46CFF);

    auto* browseBtn = body->makeChild<sc::ui::Button>();
    browseBtn->setText(tr("save.browse"));
    browseBtn->setPosition(308.f, folderY);
    browseBtn->setSize(52.f, 28.f);
    browseBtn->setJustifyContent(sc::ui::Justify::Center);
    browseBtn->setAlignItems(sc::ui::Align::Center);
    browseBtn->setFontSize(12.f);
    browseBtn->setBg(0xFFFFFFFF);
    browseBtn->setBorder(1.f, kBorder);
    browseBtn->setBorderRadius(4.f);
    browseBtn->hoverBgColor = 0xF2F2F2FF;
    browseBtn->onClick.add([this](sc::ui::Button*) {
        // Hide this topmost dialog so the native chooser is visible above it,
        // then restore.
        if (nameBox_) nameBox_->blur();
        if (dirBox_) dirBox_->blur();
        hide();
        auto dir = sc::platform::FileDialog::chooseDirectory(dir_.string());
        if (!dir.empty()) dir_ = dir;
        show();
        raise();
        grabKeyboardFocus();
        updateControls();
    });

    updateControls();
}

sc::ui::TextBox* SaveDialogWindow::focusedField() const {
    if (nameBox_ && nameBox_->isFocused) return nameBox_;
    if (dirBox_ && dirBox_->isFocused) return dirBox_;
    return nullptr;
}

void SaveDialogWindow::focusField(sc::ui::TextBox* box) {
    if (!box) return;
    if (nameBox_ && nameBox_ != box) nameBox_->blur();
    if (dirBox_ && dirBox_ != box) dirBox_->blur();
    box->focus();
}

void SaveDialogWindow::updateControls() {
    // The directory box always mirrors the current target directory.
    if (dirBox_) dirBox_->setText(dir_.string());
    refresh();
}

std::filesystem::path SaveDialogWindow::targetPath() const {
    if (!nameBox_ || !dirBox_) return {};
    std::string name = nameBox_->getText();
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
    size_t start = name.find_first_not_of(" \t");
    name = start == std::string::npos ? std::string() : name.substr(start);
    if (name.empty()) return {};
    if (name.find('.') == std::string::npos) name += ".png";

    std::string dir = dirBox_->getText();
    while (!dir.empty() && (dir.back() == ' ' || dir.back() == '\t' || dir.back() == '/')) dir.pop_back();
    start = dir.find_first_not_of(" \t");
    dir = start == std::string::npos ? std::string() : dir.substr(start);
    // "~" or "~/..." expands to the home directory.
    if (dir == "~") dir = g_get_home_dir();
    else if (dir.rfind("~/", 0) == 0) dir = std::string(g_get_home_dir()) + dir.substr(1);
    std::filesystem::path dirPath = dir.empty() ? dir_ : std::filesystem::path(dir);
    if (dirPath.is_relative()) dirPath = std::filesystem::current_path() / dirPath;

    // A directory that does not exist yet is created on save.
    std::error_code ec;
    if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath, ec);
        if (ec) return {};
    }

    std::filesystem::path p = dirPath / name;
    // avoid clobbering existing files: append " (n)" before the extension
    if (std::filesystem::exists(p)) {
        std::filesystem::path stem = p.stem(), ext = p.extension();
        for (int n = 1; n < 1000; ++n) {
            std::filesystem::path cand = dirPath / (stem.string() + " (" + std::to_string(n) + ")" + ext.string());
            if (!std::filesystem::exists(cand)) return cand;
        }
    }
    return p;
}

void SaveDialogWindow::finish(bool saved) {
    if (saved) {
        auto path = targetPath();
        if (path.empty()) return; // nothing to save yet: keep the dialog open
        if (image_) {
            if (!image_->saveToPng(path))
                g_warning("[save dialog] failed to write %s", path.string().c_str());
        }
    }
    bool quit = quitWhenDone_;
    auto* app = sc::core::AppContext::get();
    // Deferred teardown: never destroy this window inside its own callback.
    app->postTask([quit] {
        SaveDialogWindow::dispose();
        CaptureWindow::closeAll();
        PinWindow::dispose();
        if (quit) sc::core::AppContext::get()->quit(0);
    });
}

void SaveDialogWindow::onKeyDown(uint32_t key) {
    constexpr uint32_t GDK_ESCAPE = 0xff1b;
    constexpr uint32_t GDK_RETURN = 0xff0d;
    constexpr uint32_t GDK_KP_ENTER = 0xff8d;
    constexpr uint32_t GDK_TAB = 0xff09;
    if (key == GDK_ESCAPE) { finish(false); return; }
    if (key == GDK_RETURN || key == GDK_KP_ENTER) { finish(true); return; }

    if (key == GDK_TAB) {
        // Tab cycles focus between the two text fields.
        if (nameBox_ && nameBox_->isFocused) focusField(dirBox_);
        else if (dirBox_ && dirBox_->isFocused) focusField(nameBox_);
        else if (nameBox_) focusField(nameBox_);
        refresh();
        return;
    }

    sc::ui::TextBox* box = focusedField();
    if (!box) return;
    constexpr uint32_t GDK_KEY_BACKSPACE_K = 0xff08;
    constexpr uint32_t GDK_KEY_LEFT = 0xff51;
    constexpr uint32_t GDK_KEY_RIGHT = 0xff53;
    constexpr uint32_t GDK_KEY_HOME = 0xff50;
    constexpr uint32_t GDK_KEY_END = 0xff57;
    constexpr uint32_t GDK_KEY_DELETE = 0xffff;
    bool shift = shiftHeld();
    bool handled = false;
    switch (key) {
        case GDK_KEY_BACKSPACE_K: handled = box->backspace(); break;
        case GDK_KEY_DELETE: handled = box->deleteChar(); break;
        case GDK_KEY_LEFT: handled = box->caretMove(-1, shift); break;
        case GDK_KEY_RIGHT: handled = box->caretMove(1, shift); break;
        case GDK_KEY_HOME: box->caretToStart(shift); handled = true; break;
        case GDK_KEY_END: box->caretToEnd(shift); handled = true; break;
        default: break;
    }
    if (handled) refresh();
}

void SaveDialogWindow::onTextInput(const std::string& utf8) {
    if (auto* box = focusedField()) {
        box->insertAtCaret(utf8);
        refresh();
    }
}

void SaveDialogWindow::onMouseDown(const core::PointF& pos, bool right) {
    if (right) return;
    // Clicking anywhere brings the X keyboard focus back to this dialog
    // (override-redirect windows never regain focus on their own).
    grabKeyboardFocus();
    auto* hit = body->hitTest(pos.x, pos.y);
    if (pos.y <= 40.f && hit && !dynamic_cast<sc::ui::Button*>(hit)) {
        // Top action bar: start a window drag (buttons excluded). Grab the
        // pointer so motion events keep flowing when the cursor outruns the
        // window; coordinates are relative to our (moving) origin, hence the
        // absolute-pointer formula in onMouseMove.
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
    if (!nameBox_ || !dirBox_) return;
    if (hit == nameBox_ || hit == nameField_) {
        focusField(nameBox_);
        nameBox_->setCaretByte(nameBox_->caretIndexFromX(pos.x, dpi));
        fieldPressing_ = true;
        pressBox_ = nameBox_;
        refresh();
    } else if (hit == dirBox_ || hit == dirField_) {
        focusField(dirBox_);
        dirBox_->setCaretByte(dirBox_->caretIndexFromX(pos.x, dpi));
        fieldPressing_ = true;
        pressBox_ = dirBox_;
        refresh();
    } else {
        nameBox_->blur();
        dirBox_->blur();
    }
}

void SaveDialogWindow::onMouseMove(const core::PointF& pos) {
    if (fieldPressing_ && pressBox_ && pressBox_->isFocused) {
        pressBox_->setCaretExtend(pressBox_->caretIndexFromX(pos.x, dpi));
        refresh();
        return; // text selection drag, not a window drag
    }
    if (!dragging_) return;
    // pos is relative to this window's origin, which moves with each move();
    // absolute pointer position (x + pos.x) minus the press-time pointer
    // position gives the stable drag delta.
    int nx = winX0_ + static_cast<int>(x + pos.x - absPressX_);
    int ny = winY0_ + static_cast<int>(y + pos.y - absPressY_);
    x = nx;
    y = ny;
    if (native()) {
        gtk_window_move(GTK_WINDOW(native()), nx, ny);
        if (GdkWindow* gw = gtk_widget_get_window(native())) gdk_window_move(gw, nx, ny);
    }
}

void SaveDialogWindow::onMouseUp(const core::PointF& pos, bool right) {
    fieldPressing_ = false;
    pressBox_ = nullptr;
    if (!dragging_) return;
    dragging_ = false;
    if (GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default())) {
        gdk_seat_ungrab(seat);
    }
}

} // namespace sc::windows
