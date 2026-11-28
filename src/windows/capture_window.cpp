#include "screencapture/windows/capture_window.h"
#include "screencapture/windows/save_dialog.h"
#include "screencapture/windows/tool_bars.h"
#include "screencapture/windows/pin_window.h"
#include "screencapture/core/app_context.h"
#include "screencapture/core/config_manager.h"
#include "screencapture/core/i18n.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/x11_error_trap.h"
#include "screencapture/platform/screen_capturer.h"
#include "screencapture/platform/input_injector.h"
#include "screencapture/platform/clipboard_service.h"
#include "screencapture/platform/file_dialog.h"
#include "screencapture/media/ocr_runner.h"
#include "screencapture/media/qr_scanner.h"
#include "screencapture/gfx/text_layout.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace sc::windows {

std::vector<std::unique_ptr<CaptureWindow>> CaptureWindow::instances_;
bool CaptureWindow::suppressAutoQuit_ = false;
bool CaptureWindow::videoExporting_ = false;

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Grab `rect` and wait until the pixels stop changing (two identical snapshots
// in a row, a few attempts max). A frame caught in the middle of a smooth
// scroll / hover animation matches at the wrong offset in the stitcher and
// leaves duplicated ("ghost") bands in the long screenshot.
static bool captureSettled(const core::RectI& rect, int w, int h,
                           std::vector<uint8_t>& out, bool& settled) {
    settled = false;
    const size_t bytes = static_cast<size_t>(w) * h * 4;
    out.resize(bytes);
    std::vector<uint8_t> prev;
    constexpr int kMaxAttempts = 8;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        auto frame = sc::platform::ScreenCapturer::capture(rect.left, rect.top, w, h);
        if (!frame) return false;
        frame->flush();
        std::memcpy(out.data(), frame->data(), bytes);
        if (!prev.empty() && prev == out) {
            settled = true;
            return true;
        }
        prev = out;
        if (attempt + 1 < kMaxAttempts) g_usleep(70 * 1000);
    }
    return true;  // 返回最后一张，由调用方决定是否重试
}

static void showInfoDialog(const std::string& title, const std::string& message) {
    GtkWidget* dlg = gtk_message_dialog_new(
        nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        " ");
    gtk_window_set_title(GTK_WINDOW(dlg), title.c_str());
    gtk_window_set_keep_above(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);

    // Use our own plain-text label. GtkMessageDialog's built-in label treats its
    // input as Pango markup, which can hide OCR output containing <tags> or '&'.
    GtkWidget* area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dlg));
    GList* oldChildren = gtk_container_get_children(GTK_CONTAINER(area));
    for (GList* item = oldChildren; item; item = item->next) {
        GtkWidget* child = GTK_WIDGET(item->data);
        gtk_container_remove(GTK_CONTAINER(area), child);
        gtk_widget_destroy(child);
    }
    g_list_free(oldChildren);

    GtkWidget* label = gtk_label_new(message.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);

    // A full-screen OCR selection can produce thousands of characters. A plain
    // GTK message dialog grows beyond the monitor, putting its OK button out of
    // reach. Keep long results in a bounded, scrollable area on every desktop.
    const size_t lineCount = static_cast<size_t>(std::count(message.begin(), message.end(), '\n'));
    if (message.size() > 2000 || lineCount > 24) {
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        GdkRectangle workArea{};
        GdkMonitor* monitor = gdk_display_get_primary_monitor(gdk_display_get_default());
        if (!monitor) monitor = gdk_display_get_monitor(gdk_display_get_default(), 0);
        if (monitor) gdk_monitor_get_workarea(monitor, &workArea);
        const int maxHeight = workArea.height > 180 ? (workArea.height - 180) * 3 / 5 : 120;
        const int maxWidth = workArea.width > 80 ? (workArea.width - 80) * 3 / 5 : 240;
        gtk_label_set_width_chars(GTK_LABEL(label), std::clamp(maxWidth / 8, 20, 72));
        gtk_widget_set_size_request(scroll, std::min(720, std::max(240, maxWidth)),
                                   std::min(600, std::max(120, maxHeight)));
        gtk_container_add(GTK_CONTAINER(scroll), label);
        gtk_box_pack_start(GTK_BOX(area), scroll, TRUE, TRUE, 0);
    } else {
        gtk_label_set_max_width_chars(GTK_LABEL(label), 90);
        gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 0);
    }

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_hide(dlg);
    gtk_widget_destroy(dlg);
}

// Independent lightweight topmost HUD overlay for the 3-2-1 recording countdown.
// Kept separate from the full-screen CaptureWindow so it never interferes with
// XShape hole punching or window compositing on Mutter / X11.
class VideoCountdownWindow : public sc::ui::BaseWindow {
public:
    VideoCountdownWindow(std::function<void()> onComplete, std::function<void()> onCancel)
        : onComplete_(std::move(onComplete)), onCancel_(std::move(onCancel)) {}

    void start(const core::RectF& centerBounds) {
        remaining_ = 3;
        constexpr float kW = 130.f, kH = 130.f;
        float left = centerBounds.left + (centerBounds.width() - kW) * 0.5f;
        float top = centerBounds.top + (centerBounds.height() - kH) * 0.5f;
        create("Countdown", core::RectF::fromXYWH(left, top, kW, kH),
               {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow, sc::ui::WindowFlag::NoActivate});
        if (native() && gtk_widget_get_realized(native())) {
            Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
            GdkWindow* gw = gtk_widget_get_window(native());
            if (dpy && gw) {
                Window xw = GDK_WINDOW_XID(gw);
                XRaiseWindow(dpy, xw);
                XFlush(dpy);
            }
        }
        setTimer(1000, 100);
        refresh();
    }

    void cancel() {
        killTimer(100);
        auto cancelFn = std::move(onCancel_);
        close();
        if (cancelFn) cancelFn();
    }

    void onTimer(uint32_t id) override {
        if (id == 100) {
            --remaining_;
            if (remaining_ > 0) {
                refresh();
                setTimer(1000, 100);
            } else {
                killTimer(100);
                auto done = std::move(onComplete_);
                close();
                if (done) done();
            }
        }
    }

    void onPaint(gfx::CanvasContext& ctx) override {
        ctx.clear({0.f, 0.f, 0.f, 0.f});
        core::RectF card = core::RectF::fromXYWH(2.f * dpi, 2.f * dpi, width - 4.f * dpi, height - 4.f * dpi);
        ctx.fillRoundedRect(card, 16.f * dpi, core::ColorRgba::fromRgbaHex(0x18181880));
        ctx.strokeRoundedRect(card, 16.f * dpi, core::ColorRgba::fromRgbaHex(0xFF4D4FA0), 2.f * dpi);

        std::string numStr = std::to_string(remaining_);
        gfx::TextLayout num(numStr, 48.f * dpi, 1000.f, 1000.f, "Sans Bold");
        auto m = num.metrics();
        ctx.drawText((width - m.width) * 0.5f,
                     (height - m.height) * 0.5f - 12.f * dpi,
                     num, core::ColorRgba::fromHex(0xFFFFFF));

        std::string escStr = sc::core::I18n::get()->get("hint.escCancel");
        gfx::TextLayout hint(escStr, 11.f * dpi);
        auto hm = hint.metrics();
        ctx.drawText((width - hm.width) * 0.5f,
                     height - hm.height - 12.f * dpi,
                     hint, core::ColorRgba::fromRgbaHex(0xFFFFFFB0));
    }

    void onKeyDown(uint32_t key) override {
        constexpr uint32_t ESC = 0xff1b;
        if (key == ESC) cancel();
    }

    void onMouseDown(const core::PointF&, bool) override {
        cancel();
    }

private:
    int remaining_{3};
    std::function<void()> onComplete_;
    std::function<void()> onCancel_;
};

// Resolve the OCR plugin: explicit config override, then the bundled RapidOCR
// plugin — dev layout next to the executable, system layout under share/
// (/usr/bin/screencapture + /usr/share/simplescreenshot/assets).
static std::string resolveOcrPlugin() {
    auto* cfg = sc::core::ConfigManager::get();
    std::string custom = cfg->getOcrPlugin();
    if (!custom.empty()) return custom;
    const std::filesystem::path dirs[] = {
        cfg->exeDir() / "assets",
        cfg->exeDir().parent_path() / "share" / "simplescreenshot" / "assets",
        cfg->dataPath() / "assets",
    };
    for (const auto& d : dirs) {
        std::error_code ec;
        auto p = d / "plugins" / "ocr_rapidocr.py";
        if (std::filesystem::exists(p, ec)) return p.string();
    }
    return {};
}

void CaptureWindow::init() {
    if (hasWindow()) return;
    dispose();
    suppressAutoQuit_ = false;
    auto* display = sc::platform::X11Display::get();
    display->refreshMonitors();
    auto monitors = display->monitors();
    if (monitors.empty()) monitors.push_back(display->virtualDesktop());

    // 1. Enumerate visible windows and pre-capture ALL monitors BEFORE creating
    //    or mapping any overlay window so no overlay dims another monitor's capture.
    auto visibleWins = display->enumerateVisibleWindows();
    std::vector<std::shared_ptr<gfx::ImageSurface>> preCaptured;
    preCaptured.reserve(monitors.size());
    for (const auto& m : monitors) {
        if (m.width() < 1 || m.height() < 1) {
            preCaptured.push_back(nullptr);
            continue;
        }
        preCaptured.push_back(
            sc::platform::ScreenCapturer::capture(m.left, m.top, m.width(), m.height()));
    }

    // 2. Create per-monitor overlay windows (with RGBA visual so long/video stages can be transparent).
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        if (m.width() < 1 || m.height() < 1) continue;
        auto window = std::make_unique<CaptureWindow>();
        window->monitor_ = m;
        window->screenImage_ = preCaptured[i];
        window->visibleWindows_ = visibleWins;
        window->create("ScreenCapture",
            core::RectF::fromXYWH(static_cast<float>(m.left), static_cast<float>(m.top),
                                  static_cast<float>(m.width()), static_cast<float>(m.height())),
            {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow});
        instances_.push_back(std::move(window));
    }

    auto pt = display->pointerPosition();
    for (auto& w : instances_) {
        const auto& m = w->monitor_;
        if (pt.x >= m.left && pt.x < m.right && pt.y >= m.top && pt.y < m.bottom) {
            w->isCurrentScreen_ = true;
            w->mousePos_ = {static_cast<float>(pt.x - m.left), static_cast<float>(pt.y - m.top)};
            w->cutMask_->highlight(w->mousePos_, w->visibleWindows_, {m.left, m.top});
            w->grabKeyboardFocus();
        } else {
            w->isCurrentScreen_ = false;
        }
        w->refresh();
    }
}

CaptureWindow* CaptureWindow::get() {
    if (instances_.empty()) return nullptr;
    auto p = sc::platform::X11Display::get()->pointerPosition();
    for (auto& w : instances_) {
        const auto& m = w->monitor_;
        if (p.x >= m.left && p.x < m.right && p.y >= m.top && p.y < m.bottom) return w.get();
    }
    return instances_.front().get();
}

CaptureWindow::~CaptureWindow() {
    closeAllTools();
}

void CaptureWindow::dispose() {
    for (auto& w : instances_) {
        if (w && !w->closed_) {
            w->closed_ = true;
            w->close();
        }
    }
    instances_.clear();
}

bool CaptureWindow::hasWindow() {
    for (const auto& w : instances_) {
        if (w && !w->closed_ && w->native() != nullptr) {
            return true;
        }
    }
    return false;
}

void CaptureWindow::closeAll() {
    for (auto& w : instances_) {
        if (w && !w->closed_) {
            w->closed_ = true;
            w->close();
        }
    }
    sc::core::AppContext::get()->postTask([] {
        dispose();
    });
}

void CaptureWindow::setOverlaysVisible(bool visible) {
    for (auto& w : instances_) {
        if (!w || w->closed_) continue;
        if (visible) w->show();
        else w->hide();
    }
}

void CaptureWindow::closeOtherOverlays() {
    for (auto& w : instances_) {
        if (w && w.get() != this && !w->closed_) w->close();
    }
}

void CaptureWindow::onCreated() {
    if (!screenImage_) {
        screenImage_ = sc::platform::ScreenCapturer::capture(
            monitor_.left, monitor_.top, monitor_.width(), monitor_.height());
    }
    cutMask_ = std::make_unique<CutMask>(dpi);
    cutMask_->setBounds(width, height);
    refresh();
}

void CaptureWindow::onShown() {
    if (isCurrentScreen_) {
        grabKeyboardFocus();
    }
}

void CaptureWindow::paintMagnifier(gfx::CanvasContext& ctx, const core::PointF& pos) {
    if (!screenImage_ || !screenImage_->valid()) return;
    screenImage_->flush();

    const float boxW = 152.f * dpi;
    const float boxH = 132.f * dpi;
    const float previewH = 74.f * dpi;

    float bx = pos.x + 18.f * dpi;
    float by = pos.y + 22.f * dpi;
    if (bx + boxW > width - 8.f * dpi) bx = pos.x - boxW - 18.f * dpi;
    if (by + boxH > height - 8.f * dpi) by = pos.y - boxH - 18.f * dpi;
    bx = std::max(4.f, bx);
    by = std::max(4.f, by);

    core::RectF card = core::RectF::fromXYWH(bx, by, boxW, boxH);
    ctx.fillRoundedRect(card, 6.f * dpi, core::ColorRgba::fromRgbaHex(0x1E1E1EE8));

    const float zoom = 5.f;
    const int cx = std::clamp(static_cast<int>(pos.x), 0, static_cast<int>(screenImage_->width()) - 1);
    const int cy = std::clamp(static_cast<int>(pos.y), 0, static_cast<int>(screenImage_->height()) - 1);

    const uint8_t* data = screenImage_->data();
    const int stride = screenImage_->stride();

    // Fast path: hardware-accelerated nearest-neighbor blit from screen image
    // instead of ~465 vector rect fills per motion frame.
    ctx.save();
    cairo_rectangle(ctx.native(), bx + 1.f, by + 1.f, boxW - 2.f, previewH - 1.f);
    cairo_clip(ctx.native());
    cairo_translate(ctx.native(), bx + boxW * 0.5f, by + previewH * 0.5f);
    cairo_scale(ctx.native(), zoom, zoom);
    cairo_translate(ctx.native(), -static_cast<double>(cx) - 0.5, -static_cast<double>(cy) - 0.5);
    cairo_set_source_surface(ctx.native(), screenImage_->native(), 0, 0);
    cairo_pattern_set_filter(cairo_get_source(ctx.native()), CAIRO_FILTER_NEAREST);
    cairo_paint(ctx.native());
    ctx.restore();

    auto crossColor = core::ColorRgba::fromRgbaHex(0x1677FFCC);
    ctx.fillRect(core::RectF::fromXYWH(bx + 2.f, by + previewH * 0.5f - 1.f, boxW - 4.f, 2.f), crossColor);
    ctx.fillRect(core::RectF::fromXYWH(bx + boxW * 0.5f - 1.f, by + 2.f, 2.f, previewH - 4.f), crossColor);

    ctx.strokeRect(core::RectF::fromXYWH(bx, by, boxW, previewH), core::ColorRgba::fromRgbaHex(0xFFFFFF44), 1.f);
    ctx.strokeRoundedRect(card, 6.f * dpi, core::ColorRgba::fromRgbaHex(0xFFFFFF55), 1.f);

    const uint8_t* cpx = data + cy * stride + cx * 4;
    int r = cpx[2], g = cpx[1], b = cpx[0];
    char line1[64], line2[64], line3[64];
    std::snprintf(line1, sizeof(line1), "POS (%d, %d)", cx + monitor_.left, cy + monitor_.top);
    std::snprintf(line2, sizeof(line2), "RGB (%d,%d,%d) #%02X%02X%02X", r, g, b, r, g, b);
    std::snprintf(line3, sizeof(line3), "%s",
                  sc::core::I18n::get()->get("cap.magnifierCopy").c_str());

    if (!magText1_ || magDpi_ != dpi) {
        magDpi_ = dpi;
        magText1_ = std::make_unique<gfx::TextLayout>("", 8.5f * dpi);
        magText2_ = std::make_unique<gfx::TextLayout>("", 8.5f * dpi);
        magText3_ = std::make_unique<gfx::TextLayout>("", 8.0f * dpi);
    }
    magText1_->setText(line1);
    magText2_->setText(line2);
    magText3_->setText(line3);
    ctx.drawText(bx + 6.f * dpi, by + previewH + 4.f * dpi, *magText1_, core::ColorRgba::fromHex(0xFFFFFF));
    ctx.drawText(bx + 6.f * dpi, by + previewH + 20.f * dpi, *magText2_, core::ColorRgba::fromHex(0x91CAFF));
    ctx.drawText(bx + 6.f * dpi, by + previewH + 36.f * dpi, *magText3_, core::ColorRgba::fromHex(0xBBBBBB));
}

void CaptureWindow::onPaint(gfx::CanvasContext& ctx) {
    ctx.clear({0.f, 0.f, 0.f, 0.f});

    if (stage_ == CapStage::Long || stage_ == CapStage::Video) {
        auto r = cutMask_->maskRect;
        auto dim = core::ColorRgba::fromRgbaHex(0x00000075);
        if (r.top > 0) ctx.fillRect({0, 0, width, r.top}, dim);
        if (r.bottom < height) ctx.fillRect({0, r.bottom, width, height}, dim);
        if (r.left > 0) ctx.fillRect({0, r.top, r.left, r.bottom}, dim);
        if (r.right < width) ctx.fillRect({r.right, r.top, width, r.bottom}, dim);

        auto borderCol = (stage_ == CapStage::Video)
            ? core::ColorRgba::fromHex(0xFF4D4F)
            : core::ColorRgba::fromHex(0x1677FF);
        ctx.strokeRect(r, borderCol, 2.f * dpi);

        if (stage_ == CapStage::Long && longResult_ && longResult_->valid()) {
            float pw = 180.f * dpi;
            float ph = std::min(height - 40.f,
                pw * static_cast<float>(longResult_->height()) / std::max(1u, longResult_->width()));
            // The preview is painted by this topmost overlay. Never place it
            // inside the capture hole, or it becomes a picture-in-picture
            // ghost in every subsequent frame.
            float px;
            if (r.left >= pw + 16.f) px = r.left - pw - 16.f;
            else if (r.right + pw + 16.f <= width) px = r.right + 16.f;
            else return;
            float py = std::max(16.f, r.top);
            core::RectF prevRect = core::RectF::fromXYWH(px, py, pw, ph);
            ctx.fillRoundedRect(core::RectF::fromXYWH(px - 4.f, py - 4.f, pw + 8.f, ph + 8.f),
                                4.f, core::ColorRgba::fromRgbaHex(0x1E1E1EDD));
            ctx.drawImage(*longResult_, prevRect);
            ctx.strokeRect(prevRect, core::ColorRgba::fromHex(0x1677FF), 1.5f);
        }
        return;
    }

    if (screenImage_) {
        ctx.drawImage(*screenImage_, {0, 0, width, height});
    }
    if (isCurrentScreen_ && cutMask_ && cutMask_->hasRect()) {
        cutMask_->paint(ctx);
        if (stage_ == CapStage::Select || (stage_ == CapStage::Adjust && isPress_)) {
            paintMagnifier(ctx, mousePos_);
        }
    } else {
        // Inactive monitors or empty selection: 46% black dim overlay
        ctx.fillRect({0, 0, width, height}, core::ColorRgba::fromRgbaHex(0x00000075));
        if (isCurrentScreen_ && stage_ == CapStage::Select) {
            ctx.strokeRect({0, 0, width, height}, core::ColorRgba::fromHex(0x1677FF), 2.f * dpi);
        }
    }
}

void CaptureWindow::updateCursorForPos(const core::PointF& pos) {
    if (stage_ == CapStage::Select) {
        setCursorByHint(3); // crosshair
        return;
    }
    if (stage_ == CapStage::Adjust && cutMask_) {
        MaskHit hit = cutMask_->hitTestAt(pos);
        switch (hit) {
            case MaskHit::Inside:      setCursorByHint(1); break; // move
            case MaskHit::TopLeft:
            case MaskHit::BottomRight: setCursorByHint(4); break; // nwse
            case MaskHit::TopRight:
            case MaskHit::BottomLeft:  setCursorByHint(5); break; // nesw
            case MaskHit::Top:
            case MaskHit::Bottom:      setCursorByHint(6); break; // ns
            case MaskHit::Left:
            case MaskHit::Right:       setCursorByHint(7); break; // ew
            default:                   setCursorByHint(3); break;
        }
    } else {
        setCursorByHint(0);
    }
}

void CaptureWindow::onMouseDown(const core::PointF& pos, bool right) {
    mousePos_ = pos;

    // During in-place annotation (CapStage::Annotate), keep the dark backdrop alive
    // and ensure PinWindow + ToolMain + ToolSub stay raised above the backdrop.
    if (stage_ == CapStage::Annotate) {
        int screenX = x + static_cast<int>(pos.x);
        int screenY = y + static_cast<int>(pos.y);
        if (!right && PinWindow::tryHandleToolbarClick(screenX, screenY)) {
            PinWindow::raiseAll();
            return;
        }
        PinWindow::raiseAll();
        return;
    }

    if (right) {
        if (stage_ == CapStage::Adjust) {
            stage_ = CapStage::Select;
            cutMask_->showHandles = false;
            if (toolCap_) toolCap_->hide();
            cutMask_->highlight(pos, visibleWindows_, {monitor_.left, monitor_.top});
            refresh();
        } else {
            closeAll();
        }
        return;
    }

    int screenX = x + static_cast<int>(pos.x);
    int screenY = y + static_cast<int>(pos.y);

    if (toolCap_ && toolCap_->tryHandleScreenClick(screenX, screenY)) return;
    if (toolLong_ && toolLong_->tryHandleScreenClick(screenX, screenY)) return;
    if (toolVideo_ && toolVideo_->tryHandleScreenClick(screenX, screenY)) return;

    if (stage_ == CapStage::Long || stage_ == CapStage::Video) return;

    int64_t t = nowMs();
    if (stage_ == CapStage::Adjust &&
        (t - lastDownTime_ <= 350) &&
        std::hypot(pos.x - lastDownPos_.x, pos.y - lastDownPos_.y) <= 6.f &&
        cutMask_->maskRect.contains(pos.x, pos.y)) {
        copyToClipboard();
        return;
    }
    lastDownTime_ = t;
    lastDownPos_ = pos;

    if (stage_ == CapStage::Select) {
        if (!isCurrentScreen_) {
            for (auto& w : instances_) {
                w->isCurrentScreen_ = (w.get() == this);
                w->refresh();
            }
        }
        isPress_ = true;
        cutMask_->startMakeRect(pos);
    } else if (stage_ == CapStage::Adjust) {
        isPress_ = true;
        cutMask_->startAdjust(pos);
        if (toolCap_) toolCap_->hide();
    }
    updateCursorForPos(pos);
}

void CaptureWindow::onMouseMove(const core::PointF& pos) {
    mousePos_ = pos;
    if (stage_ == CapStage::Annotate || stage_ == CapStage::Long || stage_ == CapStage::Video) return;

    if (isPress_ && stage_ == CapStage::Select) {
        cutMask_->makeRect(pos);
        refresh();
    } else if (isPress_ && stage_ == CapStage::Adjust) {
        cutMask_->adjust(pos);
        refresh();
    } else if (stage_ == CapStage::Select) {
        if (!isCurrentScreen_) {
            for (auto& w : instances_) {
                if (w.get() == this) {
                    w->isCurrentScreen_ = true;
                    w->grabKeyboardFocus();
                } else {
                    w->isCurrentScreen_ = false;
                }
                w->refresh();
            }
        }
        cutMask_->highlight(pos, visibleWindows_, {monitor_.left, monitor_.top});
        refresh();
        updateCursorForPos(pos);
    } else if (stage_ == CapStage::Adjust) {
        updateCursorForPos(pos);
    }
}

void CaptureWindow::onMouseUp(const core::PointF& pos, bool right) {
    mousePos_ = pos;
    if (stage_ == CapStage::Annotate) return;
    if (right || !isPress_) return;
    isPress_ = false;

    if (stage_ == CapStage::Select) {
        if (!cutMask_->hasRect() || cutMask_->maskRect.width() < 5.f || cutMask_->maskRect.height() < 5.f) {
            cutMask_->highlight(pos, visibleWindows_, {monitor_.left, monitor_.top});
        }
        if (!cutMask_->hasRect()) return;
        if (enterByArg()) return;
        if (sc::platform::X11Display::get()->controlDown()) { startPin(); return; }
        stage_ = CapStage::Adjust;
        cutMask_->showHandles = true;
        makeToolCap();
        refresh();
    } else if (stage_ == CapStage::Adjust) {
        if (toolCap_) {
            layoutTool(toolCap_.get());
            toolCap_->show();
            toolCap_->raise();
        }
        refresh();
    }
}

void CaptureWindow::onKeyDown(uint32_t key) {
    constexpr uint32_t GDK_ESCAPE = 0xff1b;
    constexpr uint32_t GDK_RETURN = 0xff0d;
    constexpr uint32_t GDK_KP_ENTER = 0xff8d;
    constexpr uint32_t GDK_LEFT = 0xff51;
    constexpr uint32_t GDK_UP = 0xff52;
    constexpr uint32_t GDK_RIGHT = 0xff53;
    constexpr uint32_t GDK_DOWN = 0xff54;

    if (key == GDK_ESCAPE) {
        if (stage_ == CapStage::Long) killTimer(kScrollMsg);
        if (stage_ == CapStage::Video) {
            if (countdownWindow_) {
                countdownWindow_->cancel();
                countdownWindow_.reset();
            }
            killTimer(kVideoTickMsg);
            if (videoRecorder_ && videoRecorder_->recording()) videoRecorder_->cancel();
        }
        if (stage_ == CapStage::Annotate) {
            PinWindow::closeInPlace();
        }
        closeAll();
        return;
    }

    bool ctrl = sc::platform::X11Display::get()->controlDown();
    if (key == GDK_RETURN || key == GDK_KP_ENTER || (ctrl && (key == 'c' || key == 'C'))) {
        copyCurrentStage();
        return;
    }
    if (ctrl && (key == 's' || key == 'S')) {
        saveToFile();
        return;
    }
    if (ctrl && (key == 'p' || key == 'P')) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d,%d",
                      static_cast<int>(mousePos_.x) + monitor_.left,
                      static_cast<int>(mousePos_.y) + monitor_.top);
        sc::platform::ClipboardService::setText(buf);
        closeAll();
        return;
    }
    if (screenImage_ && screenImage_->valid() && ctrl &&
        (key == 'r' || key == 'R' || key == 'h' || key == 'H' || key == 'k' || key == 'K')) {
        screenImage_->flush();
        int cx = std::clamp(static_cast<int>(mousePos_.x), 0, static_cast<int>(screenImage_->width()) - 1);
        int cy = std::clamp(static_cast<int>(mousePos_.y), 0, static_cast<int>(screenImage_->height()) - 1);
        const uint8_t* px = screenImage_->data() + cy * screenImage_->stride() + cx * 4;
        int r = px[2], g = px[1], b = px[0];
        char buf[64];
        if (key == 'r' || key == 'R') {
            std::snprintf(buf, sizeof(buf), "%d,%d,%d", r, g, b);
        } else if (key == 'h' || key == 'H') {
            std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
        } else {
            float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
            float k = 1.f - std::max({rf, gf, bf});
            float c = (k < 1.f) ? (1.f - rf - k) / (1.f - k) : 0.f;
            float m = (k < 1.f) ? (1.f - gf - k) / (1.f - k) : 0.f;
            float y = (k < 1.f) ? (1.f - bf - k) / (1.f - k) : 0.f;
            std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d",
                          static_cast<int>(std::round(c * 100.f)),
                          static_cast<int>(std::round(m * 100.f)),
                          static_cast<int>(std::round(y * 100.f)),
                          static_cast<int>(std::round(k * 100.f)));
        }
        sc::platform::ClipboardService::setText(buf);
        closeAll();
        return;
    }

    if (stage_ == CapStage::Adjust && cutMask_) {
        float dx = 0.f, dy = 0.f;
        if (key == GDK_LEFT) dx = -1.f;
        else if (key == GDK_RIGHT) dx = 1.f;
        else if (key == GDK_UP) dy = -1.f;
        else if (key == GDK_DOWN) dy = 1.f;
        if (dx != 0.f || dy != 0.f) {
            auto& r = cutMask_->maskRect;
            float rw = r.width(), rh = r.height();
            r.left = std::clamp(r.left + dx, 0.f, std::max(0.f, width - rw));
            r.top = std::clamp(r.top + dy, 0.f, std::max(0.f, height - rh));
            r.right = r.left + rw;
            r.bottom = r.top + rh;
            if (toolCap_) layoutTool(toolCap_.get());
            refresh();
        }
    }
}

void CaptureWindow::closeAllTools() {
    if (toolCap_) { toolCap_->close(); toolCap_.reset(); }
    if (toolLong_) { toolLong_->close(); toolLong_.reset(); }
    if (toolVideo_) { toolVideo_->close(); toolVideo_.reset(); }
}

void CaptureWindow::onDestroy() {
    closed_ = true;
    killTimer(kScrollMsg);
    killTimer(kVideoTickMsg);
    if (countdownWindow_) {
        countdownWindow_->close();
        countdownWindow_.reset();
    }
    // Tool windows must be destroyed on the next idle, not here: they may be
    // the very windows whose button handler is on the GTK dispatch stack
    // (e.g. the video toolbar's discard button), and destroying a widget
    // inside its own signal emission leaves the event dispatch running into
    // freed BaseWindows (ASan: dynamic_cast SEGV in handleMotion).
    if (toolCap_) toolCap_->hide();
    if (toolLong_) toolLong_->hide();
    if (toolVideo_) toolVideo_->hide();
    auto* self = this;
    sc::core::AppContext::get()->postTask([self]() {
        auto it = std::find_if(instances_.begin(), instances_.end(),
            [self](const auto& p) { return p.get() == self; });
        if (it != instances_.end()) {
            self->closeAllTools();
            instances_.erase(it);
        }
        if (!suppressAutoQuit_ && instances_.empty() &&
            !sc::windows::PinWindow::hasWindow() &&
            sc::core::AppContext::get()->autoQuit()) {
            g_timeout_add(180, +[](gpointer) -> gboolean {
                sc::core::AppContext::get()->quit(0);
                return G_SOURCE_REMOVE;
            }, nullptr);
        }
    });
}

std::shared_ptr<gfx::ImageSurface> CaptureWindow::getCutImage() const {
    if (!screenImage_ || !cutMask_) return nullptr;
    return screenImage_->cloneSubRect(cutMask_->maskRect);
}

core::RectI CaptureWindow::cutRectScreen() const {
    auto r = cutMask_->maskRect;
    return {x + static_cast<int>(r.left), y + static_cast<int>(r.top),
            x + static_cast<int>(r.right), y + static_cast<int>(r.bottom)};
}

void CaptureWindow::makeToolCap() {
    toolCap_ = std::make_unique<ToolCap>();
    toolCap_->onAction = [this](const std::string& id) {
        if (id == "mark") startAnnotate();
        else if (id == "pin") startPin();
        else if (id == "long") startLong();
        else if (id == "video") startVideo();
        else if (id == "ocr") startOcr();
        else if (id == "qrcode") startQrcode();
        else if (id == "save") saveToFile();      // opens the save dialog
        else if (id == "clipboard") copyToClipboard();  // ✓: instant copy, no dialog
        else if (id == "close") closeAll();
    };
    toolCap_->onKeyDownEvent.add([this](uint32_t k) { this->onKeyDown(k); });

    auto r = cutRectScreen();
    toolCap_->create("ToolCap",
        core::RectF::fromXYWH(static_cast<float>(r.right), static_cast<float>(r.bottom), 1.f, 1.f),
        {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow, sc::ui::WindowFlag::NoActivate});
    layoutTool(toolCap_.get());
}

void CaptureWindow::layoutTool(sc::ui::BaseWindow* tool) {
    if (!tool) return;
    auto r = cutMask_->maskRect;
    float gap = cutMask_->strokeWidth + 4.f * dpi;
    float tx = r.right - tool->width;
    float ty = r.bottom + gap;
    if (ty + tool->height > height) ty = r.top - tool->height - gap;
    if (ty < 0) ty = r.bottom - tool->height - 4.f * dpi;
    tx = std::max(0.f, std::min(tx, width - tool->width));
    tool->move(x + static_cast<int>(tx), y + static_cast<int>(ty));
    tool->raise();
}

void CaptureWindow::startAnnotate() {
    auto img = getCutImage();
    if (!img || img->width() < 1 || img->height() < 1) return;
    auto rect = cutRectScreen();

    // Keep all CaptureWindow instances open in CapStage::Annotate so the 46% dark
    // overlay around the selection box remains on screen while annotating!
    if (toolCap_) toolCap_->hide();
    for (auto& w : instances_) {
        if (!w) continue;
        w->stage_ = CapStage::Annotate;
        if (w->cutMask_) {
            w->cutMask_->showHandles = false;
            w->cutMask_->hideLabel = true;
        }
        w->setCursorByHint(0);
        w->setMouseTransparent(true);
        w->refresh();
    }

    suppressAutoQuit_ = true;
    // Overlay refresh + new-window mapping race with unmap/reshape: swallow
    // transient X errors instead of letting GDK kill the process.
    sc::platform::x11ErrorTrapPush();
    PinWindow::init(img, rect.left, rect.top, /*inPlaceAnnotate=*/true);
    sc::platform::x11ErrorTrapPopIgnored();
}

void CaptureWindow::startPin() {
    auto img = getCutImage();
    if (!img || img->width() < 1 || img->height() < 1) return;
    auto rect = cutRectScreen();
    suppressAutoQuit_ = true;
    PinWindow::init(img, rect.left, rect.top, /*inPlaceAnnotate=*/false);
    closeAll();
}

void CaptureWindow::startLong() {
    closeOtherOverlays();
    stage_ = CapStage::Long;
    cutMask_->hideLabel = true;
    if (toolCap_) toolCap_->hide();

    auto rect = cutRectScreen();
    // Inset by 2px so the blue selection border is not recorded into the long screenshot
    rect.left += 2;
    rect.top += 2;
    rect.right -= 2;
    rect.bottom -= 2;
    longW_ = rect.width();
    longH_ = rect.height();
    if (longW_ < 4 || longH_ < 4) { closeAll(); return; }

    // Cut a visual and input hole inside cutMask_->maskRect so the live window
    // underneath is visible and receives scroll events.
    hollowRect(cutMask_->maskRect);
    refresh();
    gdk_display_sync(gdk_display_get_default());

    std::vector<uint8_t> pixels;
    bool settled = false;
    if (!captureSettled(rect, longW_, longH_, pixels, settled)) { closeAll(); return; }
    stitcher_ = std::make_unique<sc::media::LongStitcher>();
    stitcher_->begin(pixels.data(), longW_, longH_);
    longResult_ = std::make_shared<gfx::ImageSurface>(
        static_cast<uint32_t>(longW_), static_cast<uint32_t>(longH_),
        pixels.data(), static_cast<uint32_t>(longW_ * 4));

    toolLong_ = std::make_unique<ToolLong>();
    toolLong_->onAction = [this](const std::string& id) {
        killTimer(kScrollMsg);
        if (id == "close") {
            closeAll();
        } else if (id == "clipboard") {
            if (longResult_) sc::platform::ClipboardService::setImage(longResult_);
            closeAll();
        } else if (id == "save") {
            if (toolLong_) toolLong_->hide();
            hide();
            auto path = sc::platform::FileDialog::saveImage("png");
            if (!path.empty() && longResult_) longResult_->saveToPng(path);
            closeAll();
        } else if (id == "pin") {
            if (longResult_) {
                auto cr = cutRectScreen();
                suppressAutoQuit_ = true;
                PinWindow::init(longResult_, cr.left + 2, cr.top + 2, false);
            }
            closeAll();
        }
    };
    toolLong_->onKeyDownEvent.add([this](uint32_t k) { this->onKeyDown(k); });
    auto r = cutRectScreen();
    toolLong_->create("ToolLong",
        core::RectF::fromXYWH(static_cast<float>(r.right), static_cast<float>(r.bottom), 1.f, 1.f),
        {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow, sc::ui::WindowFlag::NoActivate});
    layoutTool(toolLong_.get());

    // Move the pointer inside the selection rectangle so scroll wheel events
    // go into the target scrollable window rather than the toolbar below it!
    sc::platform::InputInjector::setPointer(rect.left + rect.width() / 2,
                                            rect.top + rect.height() / 2);

    setTimer(300, kScrollMsg);
    refresh();
}

void CaptureWindow::longStep() {
    auto rect = cutRectScreen();
    // Inset by 2px so the blue selection border is not recorded into the long screenshot
    rect.left += 2;
    rect.top += 2;
    rect.right -= 2;
    rect.bottom -= 2;

    // If the toolbar has no free space outside the selection, keep it out of
    // the root-window grab as well. It is restored immediately afterwards.
    const core::RectI toolRect{toolLong_ ? toolLong_->x : 0,
                               toolLong_ ? toolLong_->y : 0,
                               toolLong_ ? toolLong_->x + static_cast<int>(toolLong_->width) : 0,
                               toolLong_ ? toolLong_->y + static_cast<int>(toolLong_->height) : 0};
    bool hideToolbar = toolLong_ && toolLong_->isVisible() &&
                       rect.left < toolRect.right && rect.right > toolRect.left &&
                       rect.top < toolRect.bottom && rect.bottom > toolRect.top;
    if (hideToolbar) toolLong_->hide();

    std::vector<uint8_t> pixels;
    bool settled = false;
    bool captured = captureSettled(rect, longW_, longH_, pixels, settled);
    if (hideToolbar) { toolLong_->show(); toolLong_->raise(); }
    if (!captured) { closeAll(); return; }
    // Give smooth scrolling time to settle without advancing again. The retry
    // is bounded so continuously animated pages can still be captured; after
    // that the stitcher's two-anchor validation decides whether to append.
    if (!settled && ++longUnsettledRetries_ < 3) {
        setTimer(120, kScrollMsg);
        return;
    }
    longUnsettledRetries_ = 0;
    bool reachedEnd = false;
    if (stitcher_->appendFrame(pixels.data(), reachedEnd)) {
        auto result = std::make_shared<gfx::ImageSurface>(
            stitcher_->resultWidth(), stitcher_->resultHeight());
        std::memcpy(result->data(), stitcher_->result().data(), stitcher_->result().size());
        result->markDirty();
        longResult_ = result;
        refresh();
    }
    if (reachedEnd) {
        killTimer(kScrollMsg);
        restoreInputShape();
        refresh();
        return;
    }
    hollowRect(cutMask_->maskRect);

    // Only inject scroll wheel events when the mouse is NOT hovering over ToolLong.
    // Never warp the user's pointer, allowing them to freely move to the toolbar to finish or cancel.
    auto pt = sc::platform::X11Display::get()->pointerPosition();
    bool onToolLong = toolLong_ && toolLong_->isVisible() &&
                      pt.x >= toolLong_->x && pt.x <= toolLong_->x + static_cast<int>(toolLong_->width) &&
                      pt.y >= toolLong_->y && pt.y <= toolLong_->y + static_cast<int>(toolLong_->height);
    if (!onToolLong) {
        sc::platform::InputInjector::scrollWheelDown(2);
    }
    setTimer(450, kScrollMsg);
}

void CaptureWindow::onTimer(uint32_t id) {
    if (id == kScrollMsg) {
        longStep();
    } else if (id == kVideoTickMsg && toolVideo_ && videoRecorder_ && videoRecorder_->recording()) {
        ++videoElapsedSec_;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d / %s",
                      videoElapsedSec_ / 60, videoElapsedSec_ % 60,
                      toolVideo_->isMp4() ? "120:00" : "06:00");
        toolVideo_->setTimerText(buf);
        // Timers are one-shot: re-arm for the next elapsed second.
        setTimer(1000, kVideoTickMsg);
    }
}

void CaptureWindow::startVideo() {
    closeOtherOverlays();
    stage_ = CapStage::Video;
    cutMask_->hideLabel = true;
    if (toolCap_) toolCap_->hide();

    // Hollow out the interior of the selection rectangle so the live application
    // underneath is completely visible and interactive during recording!
    hollowRect(cutMask_->maskRect);

    toolVideo_ = std::make_unique<ToolVideo>();
    toolVideo_->onAction = [this](const std::string& act) {
        if (act == "start") {
            if (countdownWindow_ || (videoRecorder_ && videoRecorder_->recording())) return;
            if (!sc::media::VideoRecorder::ffmpegAvailable()) {
                if (toolVideo_) toolVideo_->hide();
                hide();
                auto i18n = sc::core::I18n::get();
                showInfoDialog(i18n->get("about.sysTip"), i18n->get("video.ffmpegMissing"));
                closeAll();
                return;
            }

            auto r = cutRectScreen();
            core::RectF centerRect = core::RectF::fromXYWH(
                static_cast<float>(r.left), static_cast<float>(r.top),
                static_cast<float>(r.width()), static_cast<float>(r.height()));

            countdownWindow_ = std::make_unique<VideoCountdownWindow>(
                [this]() {
                    countdownWindow_.reset();
                    if (!videoRecorder_) videoRecorder_ = std::make_unique<sc::media::VideoRecorder>();
                    auto region = cutRectScreen();
                    // Inset by 2px so the red recording frame is not recorded into the video
                    region.left += 2;
                    region.top += 2;
                    region.right -= 2;
                    region.bottom -= 2;
                    bool ok = toolVideo_->isMp4()
                        ? videoRecorder_->startMp4(region, 30, toolVideo_->useSpeaker(), toolVideo_->useMic())
                        : videoRecorder_->startGif(region, 15);
                    if (ok) {
                        videoElapsedSec_ = 0;
                        toolVideo_->showRecording(true);
                        layoutTool(toolVideo_.get());
                        setTimer(1000, kVideoTickMsg);
                    } else {
                        if (toolVideo_) toolVideo_->hide();
                        hide();
                        auto i18n = sc::core::I18n::get();
                        showInfoDialog(i18n->get("about.sysTip"),
                                       sc::media::VideoRecorder::ffmpegAvailable()
                                           ? i18n->get("video.recordFailed")
                                           : i18n->get("video.ffmpegMissing"));
                        closeAll();
                    }
                },
                [this]() {
                    countdownWindow_.reset();
                }
            );
            countdownWindow_->start(centerRect);
        } else if (act == "stop" || act == "save") {
            if (countdownWindow_) { countdownWindow_->close(); countdownWindow_.reset(); }
            stopAndSaveVideo(false);
        } else if (act == "clipboard") {
            if (countdownWindow_) { countdownWindow_->close(); countdownWindow_.reset(); }
            stopAndSaveVideo(true);
        } else if (act == "close" || act == "discard") {
            if (countdownWindow_) { countdownWindow_->close(); countdownWindow_.reset(); }
            killTimer(kVideoTickMsg);
            if (videoRecorder_ && videoRecorder_->recording()) videoRecorder_->cancel();
            closeAll();
        }
    };
    toolVideo_->onKeyDownEvent.add([this](uint32_t k) { this->onKeyDown(k); });

    auto r = cutRectScreen();
    toolVideo_->create("ToolVideo",
        core::RectF::fromXYWH(static_cast<float>(r.right), static_cast<float>(r.bottom), 1.f, 1.f),
        {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow, sc::ui::WindowFlag::NoActivate});
    layoutTool(toolVideo_.get());
    refresh();
}

void CaptureWindow::stopAndSaveVideo(bool toClipboard) {
    if (videoExporting_) return;   // a second stop click while exporting
    if (countdownWindow_) {
        countdownWindow_->close();
        countdownWindow_.reset();
    }
    killTimer(kVideoTickMsg);
    if (!videoRecorder_) {
        closeAll();
        return;
    }
    if (!videoRecorder_->recording()) {
        // The encode never ran — never silently save an empty or stale file.
        if (toolVideo_) toolVideo_->hide();
        hide();
        showInfoDialog(sc::core::I18n::get()->get("about.sysTip"),
                       sc::core::I18n::get()->get("video.recordFailed"));
        closeAll();
        return;
    }

    // Stop + GIF palette pass + audio mux can take from seconds to minutes
    // on long captures: run them on a worker thread so the UI never freezes
    // (same invisible-background feel as OCR). The shared_ptr keeps the
    // recorder — and its private temp dir — alive until the completion task
    // has consumed the output file.
    suppressAutoQuit_ = true;
    const bool quitWhenDone = sc::core::AppContext::get()->autoQuit();
    std::shared_ptr<sc::media::VideoRecorder> recorder = std::move(videoRecorder_);
    videoExporting_ = true;
    if (toolVideo_) toolVideo_->hide();
    setOverlaysVisible(false);
    closeAll();

    auto* app = sc::core::AppContext::get();
    app->runBackground([app, recorder, toClipboard, quitWhenDone]() {
        recorder->stop();
        const bool valid = recorder->outputValid();
        app->postTask([app, recorder, toClipboard, quitWhenDone, valid]() {
            auto* i18n = sc::core::I18n::get();
            if (valid) {
                if (toClipboard) {
                    if (recorder->isGif()) {
                        sc::platform::ClipboardService::setGif(recorder->outputPath());
                    } else {
                        sc::platform::ClipboardService::setFile(recorder->outputPath(), "video/mp4");
                    }
                } else {
                    std::string ext = recorder->isGif() ? "gif" : "mp4";
                    auto dst = sc::platform::FileDialog::saveImage(ext);
                    if (!dst.empty()) {
                        std::error_code ec;
                        std::filesystem::copy_file(recorder->outputPath(), dst,
                            std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            } else {
                // The encode failed (missing/broken ffmpeg, failed audio device, …).
                showInfoDialog(i18n->get("about.sysTip"), i18n->get("video.recordFailed"));
            }
            videoExporting_ = false;
            suppressAutoQuit_ = false;
            if (quitWhenDone) {
                sc::core::AppContext::get()->quit(0);
            }
        });
    });
}

void CaptureWindow::startOcr() {
    auto img = getCutImage();
    if (toolCap_) toolCap_->hide();
    setOverlaysVisible(false);
    if (!img) {
        closeAll();
        return;
    }
    auto* i18n = sc::core::I18n::get();
    std::string plugin = resolveOcrPlugin();
    if (!sc::media::OcrRunner::available(plugin)) {
        showInfoDialog(i18n->get("cap.ocr"), i18n->get("cap.ocrMissing"));
        closeAll();
        return;
    }

    suppressAutoQuit_ = true;
    const bool quitWhenDone = sc::core::AppContext::get()->autoQuit();
    closeAll();

    // Managed worker thread: quit() joins it before the process starts
    // tearing down, so recognize()/postTask can never outlive the singletons.
    sc::core::AppContext::get()->runBackground([img, plugin, quitWhenDone]() {
        auto result = sc::media::OcrRunner::recognize(img, plugin);
        auto text = std::move(result.text);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
            text.pop_back();
        }
        sc::core::AppContext::get()->postTask([text, success = result.success,
                                               error = std::move(result.error), quitWhenDone]() {
            auto* i18n = sc::core::I18n::get();
            if (!success) {
                std::string message = i18n->get("cap.ocrFailed");
                if (!error.empty()) message += "\n\n" + error.substr(0, 800);
                showInfoDialog(i18n->get("cap.ocr"), message);
            } else if (!text.empty()) {
                sc::platform::ClipboardService::setText(text);
                showInfoDialog(i18n->get("cap.ocr"),
                               i18n->get("cap.ocrSuccess") + "\n\n" + text);
            } else {
                showInfoDialog(i18n->get("cap.ocr"),
                               i18n->get("cap.ocrEmpty"));
            }
            if (quitWhenDone) {
                sc::core::AppContext::get()->quit(0);
            }
        });
    });
}

void CaptureWindow::startQrcode() {
    auto img = getCutImage();
    if (toolCap_) toolCap_->hide();
    setOverlaysVisible(false);
    if (img) {
        auto text = sc::media::QrScanner::scanJoined(img);
        if (!text.empty()) {
            sc::platform::ClipboardService::setText(text);
            showInfoDialog(sc::core::I18n::get()->get("cap.qrcode"),
                           sc::core::I18n::get()->get("cap.qrcodeCopy") + "：\n\n" + text);
        } else {
            showInfoDialog(sc::core::I18n::get()->get("cap.qrcode"),
                           sc::core::I18n::get()->get("cap.qrcodeEmpty"));
        }
    }
    closeAll();
}

bool CaptureWindow::enterByArg() {
    auto mode = sc::core::AppContext::get()->enterMode();
    if (mode.empty()) return false;
    if (mode == "long") startLong();
    else if (mode == "video") startVideo();
    else if (mode == "ocr") startOcr();
    else if (mode == "qr" || mode == "qrcode") startQrcode();
    else if (mode == "pin") startPin();
    else return false;
    return true;
}

void CaptureWindow::saveToFile() {
    auto img = getCutImage();
    if (!img) return;
    // The save dialog owns the final exit in --auto-quit mode.
    suppressAutoQuit_ = true;
    sc::platform::x11ErrorTrapPush();
    if (toolCap_) toolCap_->hide();
    setOverlaysVisible(false);
    windows::SaveDialogWindow::open(img, sc::core::AppContext::get()->autoQuit());
    sc::platform::x11ErrorTrapPopIgnored();
}

void CaptureWindow::copyToClipboard() {
    auto img = getCutImage();
    if (img) sc::platform::ClipboardService::setImage(img);
    closeAll();
}

void CaptureWindow::copyCurrentStage() {
    if (stage_ == CapStage::Long && longResult_) {
        sc::platform::ClipboardService::setImage(longResult_);
        closeAll();
    } else {
        copyToClipboard();
    }
}

} // namespace sc::windows
