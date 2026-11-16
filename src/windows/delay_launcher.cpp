#include "screencapture/windows/delay_launcher.h"
#include "screencapture/core/app_context.h"
#include "screencapture/core/i18n.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/x11_error_trap.h"
#include "screencapture/windows/capture_window.h"

namespace sc::windows {

namespace {

constexpr float kW = 320.f, kH = 176.f;
constexpr uint32_t kGreen = 0x30A46CFF;
constexpr uint32_t kGreenHover = 0x2A925FFF;
constexpr uint32_t kBorder = 0xC9C9C9FF;
constexpr uint32_t kTextDim = 0x666666FF;

std::string tr(const std::string& key) { return sc::core::I18n::get()->get(key); }

std::string fmtCountdown(int n) {
    std::string s = tr("delay.countdown");
    if (auto pos = s.find("{n}"); pos != std::string::npos) {
        s.replace(pos, 3, std::to_string(n));
    }
    return s;
}

// This is a composited (GNOME/Mutter) desktop: for ~100ms after a window
// unmaps, the compositor still composites the old frame into the root-window
// image that ScreenCapturer reads — the same ghosting the pin windows fixed.
// Pump the pending GTK events so the unmap is actually sent, then give the
// compositor a frame of margin before starting the capture.
void settleBeforeCapture() {
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
    gdk_display_flush(gdk_display_get_default());
    g_usleep(200 * 1000);
    gdk_flush();
}

} // namespace

std::unique_ptr<DelayLauncherWindow> DelayLauncherWindow::instance_;

void DelayLauncherWindow::startTrayCountdown(int seconds) {
    if (seconds <= 0) { init(); return; }
    if (sc::windows::CaptureWindow::hasWindow()) return; // already capturing
    // Silent countdown on the GTK main loop; the capture fires afterwards.
    g_timeout_add_seconds(std::max(seconds, 1), +[](gpointer) -> int {
        sc::core::AppContext::get()->postTask([] {
            windows::CaptureWindow::init();
        });
        return G_SOURCE_REMOVE;
    }, nullptr);
}

void DelayLauncherWindow::init() {
    if (instance_) {
        // Re-show an existing (hidden) dialog instead of doing nothing.
        instance_->show();
        instance_->raise();
        instance_->grabKeyboardFocus();
        return;
    }
    auto* display = sc::platform::X11Display::get();
    auto monitors = display->monitors();
    if (monitors.empty()) monitors.push_back(display->virtualDesktop());
    const auto& m = monitors.front();

    instance_.reset(new DelayLauncherWindow());
    float left = static_cast<float>(m.left) + (static_cast<float>(m.width()) - kW) * 0.5f;
    float top = static_cast<float>(m.top) + (static_cast<float>(m.height()) - kH) * 0.5f;
    // No flags: a WM-managed borderless toplevel, NOT an override-redirect
    // popup — other windows must be able to cover this dialog. The WM cannot
    // drag a decorated-less window, so title-area dragging is manual (see
    // onMouseDown), same scheme as SaveDialogWindow.
    instance_->create("SimpleScreenShot", core::RectF::fromXYWH(left, top, kW, kH), {});
    // While the pointer grab is active (window drag), events arrive on the
    // toplevel window instead of the drawing area.
    instance_->connectWindowSideEvents();
    instance_->grabKeyboardFocus();
}

void DelayLauncherWindow::dispose() { instance_.reset(); }

void DelayLauncherWindow::onCreated() {
    // Borderless managed dialog: keep it out of the taskbar / alt-tab (the app
    // lives in the tray). Must happen before the window is realized.
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(native()), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(native()), TRUE);

    // Opaque light background: the RGBA visual would otherwise leave the body
    // fully transparent (rendered black by the compositor).
    body->setBg(0xFFFFFFFF);
    body->setBorder(1.f, 0xD9D9D9FF);

    // Title bar: dialog name on the left, a close glyph on the right.
    auto* title = body->makeChild<sc::ui::Label>();
    title->setText(tr("delay.title"));
    title->setFontSize(15.f);
    title->setFontFamily("Sans Bold");
    title->setPosition(18.f, 14.f);
    title->setSize(240.f, 26.f);

    auto* closeBtn = body->makeChild<sc::ui::Button>();
    closeBtn->setText("\xc3\x97"); // U+00D7 multiplication sign as the close glyph
    closeBtn->setPosition(282.f, 8.f);
    closeBtn->setSize(30.f, 26.f);
    closeBtn->setJustifyContent(sc::ui::Justify::Center);
    closeBtn->setAlignItems(sc::ui::Align::Center);
    closeBtn->setFontSize(15.f);
    closeBtn->setColor(kTextDim);
    closeBtn->setBorderRadius(6.f);
    closeBtn->hoverBgColor = 0xEDEDEDFF;
    closeBtn->onClick.add([](sc::ui::Button*) {
        sc::core::AppContext::get()->postTask([] { DelayLauncherWindow::dispose(); });
    });

    auto* divider = body->makeChild<sc::ui::WidgetNode>();
    divider->setBg(0xE8E8E8FF);
    divider->setPosition(0.f, 48.f);
    divider->setSize(kW, 1.f);

    // Delay row: "延迟秒数" label, a value box, then -/+ steppers.
    auto* secondsLabel = body->makeChild<sc::ui::Label>();
    secondsLabel->setText(tr("delay.seconds"));
    secondsLabel->setFontSize(13.f);
    secondsLabel->setPosition(18.f, 70.f);
    secondsLabel->setSize(142.f, 34.f);
    secondsLabel->setAlignItems(sc::ui::Align::Center);

    auto* valueBox = body->makeChild<sc::ui::WidgetNode>();
    valueBox->setPosition(166.f, 70.f);
    valueBox->setSize(56.f, 34.f);
    valueBox->setBg(0xFFFFFFFF);
    valueBox->setBorder(1.f, kBorder);
    valueBox->setBorderRadius(6.f);

    valueLabel_ = valueBox->makeChild<sc::ui::Label>();
    valueLabel_->setJustifyContent(sc::ui::Justify::Center);
    valueLabel_->setAlignItems(sc::ui::Align::Center);
    valueLabel_->setPosition(0.f, 0.f);
    valueLabel_->setSize(55.f, 33.f);
    valueLabel_->setFontSize(14.f);

    auto makeStepper = [this](float x, const std::string& glyph) {
        auto* btn = body->makeChild<sc::ui::Button>();
        btn->setText(glyph);
        btn->setPosition(x, 70.f);
        btn->setSize(34.f, 34.f);
        btn->setJustifyContent(sc::ui::Justify::Center);
        btn->setAlignItems(sc::ui::Align::Center);
        btn->setFontSize(15.f);
        btn->setBg(0xFFFFFFFF);
        btn->setBorder(1.f, kBorder);
        btn->setBorderRadius(6.f);
        return btn;
    };
    minusBtn_ = makeStepper(228.f, "\xe2\x88\x92"); // U+2212 minus sign
    plusBtn_ = makeStepper(268.f, "+");
    minusBtn_->onClick.add([this](sc::ui::Button*) {
        if (!counting_ && seconds_ > 0) { --seconds_; updateControls(); }
    });
    plusBtn_->onClick.add([this](sc::ui::Button*) {
        if (!counting_ && seconds_ < 3600) { ++seconds_; updateControls(); }
    });

    // Primary action: start the capture (immediately, or after the countdown).
    startBtn_ = body->makeChild<sc::ui::Button>();
    startBtn_->setPosition(18.f, 120.f);
    startBtn_->setSize(284.f, 38.f);
    startBtn_->setJustifyContent(sc::ui::Justify::Center);
    startBtn_->setAlignItems(sc::ui::Align::Center);
    startBtn_->setFontSize(14.f);
    startBtn_->setBg(kGreen);
    startBtn_->setBorderRadius(8.f);
    startBtn_->hoverBgColor = kGreenHover;
    startBtn_->setColor(0xFFFFFFFF);
    startBtn_->onClick.add([this](sc::ui::Button*) { startCapture(); });

    updateControls();
}

void DelayLauncherWindow::startCapture() {
    if (counting_) return;
    seconds_ = std::clamp(seconds_, 0, 3600);
    if (seconds_ <= 0) {
        hide();
        settleBeforeCapture();
        sc::platform::x11ErrorTrapPush();
        windows::CaptureWindow::init();
        sc::platform::x11ErrorTrapPopIgnored();
        return;
    }
    counting_ = true;
    remaining_ = seconds_;
    setTimer(1000, kCountdownTimerId);
    // Get out of the way completely: the topmost launcher must not block the
    // apps the user is preparing while the countdown runs (same as
    // gnome-screenshot, which closes its window and waits silently).
    hide();
}

void DelayLauncherWindow::cancelCountdown() {
    if (!counting_) return;
    killTimer(kCountdownTimerId);
    counting_ = false;
    remaining_ = -1;
    updateControls();
}

void DelayLauncherWindow::onTimer(uint32_t id) {
    if (id != kCountdownTimerId || !counting_) return;
    if (--remaining_ > 0) {
        updateControls();
        setTimer(1000, kCountdownTimerId);
        return;
    }
    killTimer(kCountdownTimerId);
    counting_ = false;
    remaining_ = -1;
    hide();
    settleBeforeCapture();
    sc::platform::x11ErrorTrapPush();
    windows::CaptureWindow::init();
    sc::platform::x11ErrorTrapPopIgnored();
}

void DelayLauncherWindow::onKeyDown(uint32_t key) {
    constexpr uint32_t ESC = 0xff1b;
    if (key != ESC) return;
    if (counting_) {
        cancelCountdown();
    } else {
        // Deferred so we don't destroy this window inside its own event handler.
        sc::core::AppContext::get()->postTask([] { DelayLauncherWindow::dispose(); });
    }
}

void DelayLauncherWindow::onMouseDown(const core::PointF& pos, bool right) {
    if (right) return;
    auto* hit = body->hitTest(pos.x, pos.y);
    if (pos.y <= 48.f && hit && !dynamic_cast<sc::ui::Button*>(hit)) {
        // Title area (above the divider, close button excluded): start a
        // window drag. Grab the pointer so motion events keep flowing when
        // the cursor outruns the window; coordinates are relative to our
        // (moving) origin, hence the absolute-pointer formula in onMouseMove.
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
    }
}

void DelayLauncherWindow::onMouseMove(const core::PointF& pos) {
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

void DelayLauncherWindow::onMouseUp(const core::PointF& pos, bool right) {
    if (!dragging_) return;
    dragging_ = false;
    if (GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default())) {
        gdk_seat_ungrab(seat);
    }
}

void DelayLauncherWindow::updateControls() {
    valueLabel_->setText(std::to_string(counting_ ? remaining_ : seconds_));
    const bool counting = counting_;
    valueLabel_->setColor(counting ? kGreen : 0x222222FF);
    minusBtn_->visible = !counting;
    plusBtn_->visible = !counting;
    startBtn_->setText(counting ? fmtCountdown(remaining_) : tr("delay.start"));
    startBtn_->setBg(counting ? 0x8FC7ADFF : kGreen);
    startBtn_->hoverBgColor = counting ? 0x8FC7ADFF : kGreenHover;
    refresh();
}

} // namespace sc::windows
