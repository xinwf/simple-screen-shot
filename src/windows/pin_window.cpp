#include "screencapture/windows/pin_window.h"
#include "screencapture/windows/capture_window.h"
#include "screencapture/windows/save_dialog.h"
#include "screencapture/windows/tool_bars.h"
#include "screencapture/core/app_context.h"
#include "screencapture/core/config_manager.h"
#include "screencapture/core/i18n.h"
#include "screencapture/gfx/text_layout.h"
#include "screencapture/platform/clipboard_service.h"
#include "screencapture/platform/file_dialog.h"
#include "screencapture/platform/x11_display.h"
#include "screencapture/platform/x11_error_trap.h"
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace sc::windows {

namespace {
std::vector<std::unique_ptr<PinWindow>> pinWindows;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void drawShadowMask(cairo_t* cr, float sm, float cw, float ch) {
    // 1. Top edge (linear gradient from outer 0% to inner 60% opacity)
    cairo_pattern_t* patTop = cairo_pattern_create_linear(0, 0, 0, sm);
    cairo_pattern_add_color_stop_rgba(patTop, 0.0, 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgba(patTop, 1.0, 0.0, 0.0, 0.0, 0.6);
    cairo_set_source(cr, patTop);
    cairo_rectangle(cr, sm, 0, cw, sm);
    cairo_fill(cr);
    cairo_pattern_destroy(patTop);

    // 2. Bottom edge
    cairo_pattern_t* patBottom = cairo_pattern_create_linear(0, sm + ch, 0, sm + ch + sm);
    cairo_pattern_add_color_stop_rgba(patBottom, 0.0, 0.0, 0.0, 0.0, 0.6);
    cairo_pattern_add_color_stop_rgba(patBottom, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(cr, patBottom);
    cairo_rectangle(cr, sm, sm + ch, cw, sm);
    cairo_fill(cr);
    cairo_pattern_destroy(patBottom);

    // 3. Left edge
    cairo_pattern_t* patLeft = cairo_pattern_create_linear(0, 0, sm, 0);
    cairo_pattern_add_color_stop_rgba(patLeft, 0.0, 0.0, 0.0, 0.0, 0.0);
    cairo_pattern_add_color_stop_rgba(patLeft, 1.0, 0.0, 0.0, 0.0, 0.6);
    cairo_set_source(cr, patLeft);
    cairo_rectangle(cr, 0, sm, sm, ch);
    cairo_fill(cr);
    cairo_pattern_destroy(patLeft);

    // 4. Right edge
    cairo_pattern_t* patRight = cairo_pattern_create_linear(sm + cw, 0, sm + cw + sm, 0);
    cairo_pattern_add_color_stop_rgba(patRight, 0.0, 0.0, 0.0, 0.0, 0.6);
    cairo_pattern_add_color_stop_rgba(patRight, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(cr, patRight);
    cairo_rectangle(cr, sm + cw, sm, sm, ch);
    cairo_fill(cr);
    cairo_pattern_destroy(patRight);

    // 5. Top-Left corner (radial gradient centered at inner corner (sm, sm))
    cairo_pattern_t* patTL = cairo_pattern_create_radial(sm, sm, 0, sm, sm, sm);
    cairo_pattern_add_color_stop_rgba(patTL, 0.0, 0.0, 0.0, 0.0, 0.6);
    cairo_pattern_add_color_stop_rgba(patTL, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(cr, patTL);
    cairo_rectangle(cr, 0, 0, sm, sm);
    cairo_fill(cr);
    cairo_pattern_destroy(patTL);

    // 6. Top-Right corner
    cairo_pattern_t* patTR = cairo_pattern_create_radial(sm + cw, sm, 0, sm + cw, sm, sm);
    cairo_pattern_add_color_stop_rgba(patTR, 0.0, 0.0, 0.0, 0.0, 0.6);
    cairo_pattern_add_color_stop_rgba(patTR, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(cr, patTR);
    cairo_rectangle(cr, sm + cw, 0, sm, sm);
    cairo_fill(cr);
    cairo_pattern_destroy(patTR);

    // 7. Bottom-Left corner
    cairo_pattern_t* patBL = cairo_pattern_create_radial(sm, sm + ch, 0, sm, sm + ch, sm);
    cairo_pattern_add_color_stop_rgba(patBL, 0.0, 0.0, 0.0, 0.0, 0.6);
    cairo_pattern_add_color_stop_rgba(patBL, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(cr, patBL);
    cairo_rectangle(cr, 0, sm + ch, sm, sm);
    cairo_fill(cr);
    cairo_pattern_destroy(patBL);

    // 8. Bottom-Right corner
    cairo_pattern_t* patBR = cairo_pattern_create_radial(sm + cw, sm + ch, 0, sm + cw, sm + ch, sm);
    cairo_pattern_add_color_stop_rgba(patBR, 0.0, 0.0, 0.0, 0.0, 0.6);
    cairo_pattern_add_color_stop_rgba(patBR, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(cr, patBR);
    cairo_rectangle(cr, sm + cw, sm + ch, sm, sm);
    cairo_fill(cr);
    cairo_pattern_destroy(patBR);
}
} // namespace

std::vector<std::unique_ptr<PinWindow>>& PinWindow::windows() { return pinWindows; }
bool PinWindow::hasWindow() { return !pinWindows.empty(); }
void PinWindow::dispose() { pinWindows.clear(); }

void PinWindow::raiseAll() {
    for (auto& w : pinWindows) {
        if (!w) continue;
        w->raise();
        if (w->toolMain_ && w->toolMain_->isVisible()) w->toolMain_->raise();
        if (w->toolSub_ && w->toolSub_->isVisible()) w->toolSub_->raise();
        w->grabKeyboardFocus();
    }
}

void PinWindow::closeInPlace() {
    for (auto& w : pinWindows) {
        if (w && w->inPlaceAnnotate_) {
            w->inPlaceAnnotate_ = false;
            w->close();
        }
    }
}

bool PinWindow::tryHandleToolbarClick(int screenX, int screenY) {
    for (auto& w : pinWindows) {
        if (!w) continue;
        if (w->toolSub_ && w->toolSub_->tryHandleScreenClick(screenX, screenY)) return true;
        if (w->toolMain_ && w->toolMain_->tryHandleScreenClick(screenX, screenY)) return true;
    }
    return false;
}

PinWindow* PinWindow::init(const std::shared_ptr<gfx::ImageSurface>& image, int x, int y,
                           bool inPlaceAnnotate) {
    auto ptr = std::make_unique<PinWindow>(image, x, y, inPlaceAnnotate);
    auto* raw = ptr.get();
    pinWindows.push_back(std::move(ptr));
    float sm = raw->shadowMargin();
    int ism = static_cast<int>(std::round(sm));
    raw->create("ScreenCapture Pin",
                core::RectF::fromXYWH(static_cast<float>(x - ism), static_cast<float>(y - ism),
                                      static_cast<float>(image->width() + 2 * ism),
                                      static_cast<float>(image->height() + 2 * ism)),
                {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow});
    return raw;
}

PinWindow::PinWindow(const std::shared_ptr<gfx::ImageSurface>& image, int, int, bool inPlaceAnnotate)
    : screenImage_(image), inPlaceAnnotate_(inPlaceAnnotate) {
    history_ = std::make_unique<sc::shapes::ShapeHistory>(this);
}

PinWindow::~PinWindow() = default;

void PinWindow::onCreated() {
    toolMain_ = std::make_unique<ToolMain>();
    toolMain_->onAction = [this](const std::string& id) {
        if (editingText_) {
            editingText_->finishEdit();
            refresh();
        }
        if (id == "undo") {
            shapeHover_ = nullptr;
            history_->undo();
            refresh();
        } else if (id == "redo") {
            shapeHover_ = nullptr;
            history_->redo();
            refresh();
        } else if (id == "pin") {
            detachToDesktopPin();
        } else if (id == "close") {
            close();
        } else if (id == "save") {
            saveToFile();  // opens the save dialog
        } else if (id == "clipboard") {
            copyToClipboard();  // ✓: instant copy, no dialog
        } else if (id == "rect" || id == "ellipse" || id == "arrow" || id == "number" ||
                   id == "line" || id == "pen" || id == "text" || id == "mosaic" || id == "eraser") {
            shapeHover_ = nullptr;
            toolMain_->selectTool(id);
            updateToolSubState();
            refresh();
        }
        grabKeyboardFocus();
    };
    toolMain_->onKeyDownEvent.add([this](uint32_t k) { this->onKeyDown(k); });
    toolMain_->onTextInputEvent.add([this](const std::string& s) { this->onTextInput(s); });
    toolMain_->create("ToolMain",
        core::RectF::fromXYWH(static_cast<float>(x), static_cast<float>(y), 1.f, 1.f),
        {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow, sc::ui::WindowFlag::NoActivate});

    toolSub_ = std::make_unique<ToolSub>();
    toolSub_->onStyleChanged = [this]() {
        if (editingText_) {
            editingText_->applyStyle();
            refresh();
        } else if (shapeHover_ && !shapeHover_->isUndo) {
            shapeHover_->applyStyle();
            refresh();
        }
        grabKeyboardFocus();
    };
    toolSub_->onKeyDownEvent.add([this](uint32_t k) { this->onKeyDown(k); });
    toolSub_->onTextInputEvent.add([this](const std::string& s) { this->onTextInput(s); });
    toolSub_->create("ToolSub",
        core::RectF::fromXYWH(static_cast<float>(x), static_cast<float>(y), 1.f, 1.f),
        {sc::ui::WindowFlag::Topmost, sc::ui::WindowFlag::ToolWindow, sc::ui::WindowFlag::NoActivate});
    toolSub_->hide();

    // When opened via the first icon ("图像标记" / mark) on ToolCap, pre-select
    // "rect" and open ToolSub immediately so the user can start annotating right away.
    if (inPlaceAnnotate_) {
        toolMain_->selectTool("rect");
        updateToolSubState();
    } else {
        // Desktop Pin mode: start with a clean pinned card. Right-click toggles toolbar.
        toolMain_->hide();
    }

    connectWindowSideEvents();
    layoutTools();
}

float PinWindow::shadowMargin() const {
    return inPlaceAnnotate_ ? 0.f : std::round(10.f * dpi);
}

void PinWindow::detachToDesktopPin() {
    if (inPlaceAnnotate_) {
        inPlaceAnnotate_ = false;
        if (toolMain_) toolMain_->cancelSelect();
        updateToolSubState();
        CaptureWindow::closeAll();
        float sm = shadowMargin();
        int ism = static_cast<int>(std::round(sm));
        relocateWindow(x - ism, y - ism,
                       static_cast<int>(std::round(width + 2.f * sm)),
                       static_cast<int>(std::round(height + 2.f * sm)));
        raise();
        layoutTools();
        refresh();
    } else if (toolMain_) {
        toolMain_->cancelSelect();
        updateToolSubState();
        refresh();
    }
}

void PinWindow::onPaint(gfx::CanvasContext& ctx) {
    if (!screenImage_) return;
    ctx.clear({0.f, 0.f, 0.f, 0.f});

    float sm = shadowMargin();
    float cw = width - 2.f * sm;
    float ch = height - 2.f * sm;

    if (!inPlaceAnnotate_ && sm > 0.f) {
        drawShadowMask(ctx.native(), sm, cw, ch);
    }

    ctx.save();
    ctx.translate(sm, sm);
    ctx.scale(scale_, scale_);
    ctx.drawImage(*screenImage_,
                  {0, 0, static_cast<float>(screenImage_->width()),
                   static_cast<float>(screenImage_->height())});
    for (auto& shape : history_->shapes) {
        if (!shape->isUndo) shape->paint(ctx);
    }
    if (!isMouseDown_ && shapeHover_) shapeHover_->paintDragger(ctx);
    ctx.restore();

    if (inPlaceAnnotate_) {
        ctx.strokeRect({1.f * dpi, 1.f * dpi, width - 1.f * dpi, height - 1.f * dpi},
                       core::ColorRgba::fromHex(0x1677FF), 2.f * dpi);
    } else {
        // Desktop Pin mode: multi-layer high-contrast border + inner depth shadow + corner pin badge
        // 1. Outer accent blue border
        float b1 = 1.25f * dpi;
        ctx.strokeRect({sm + b1, sm + b1, sm + cw - b1, sm + ch - b1},
                       core::ColorRgba::fromHex(0x1677FF), 2.5f * dpi);

        // 2. Inner crisp white border for strong contrast on dark or colorful backgrounds
        float b2 = 2.75f * dpi;
        ctx.strokeRect({sm + b2, sm + b2, sm + cw - b2, sm + ch - b2},
                       core::ColorRgba::fromRgbaHex(0xFFFFFFE0), 1.5f * dpi);

        // 3. Subtle inner shadow line
        float b3 = 4.f * dpi;
        ctx.strokeRect({sm + b3, sm + b3, sm + cw - b3, sm + ch - b3},
                       core::ColorRgba::fromRgbaHex(0x00000030), 1.f * dpi);

        // 4. Pin badge in top-left corner
        if (cw >= 60.f * dpi && ch >= 35.f * dpi) {
            std::string badgeText = "\xf0\x9f\x93\x8c " + sc::core::I18n::get()->get("cap.pin");
            gfx::TextLayout badgeLayout(badgeText, 10.f * dpi, 500.f, 50.f, "Sans Bold");
            auto bm = badgeLayout.metrics();
            float padX = 7.f * dpi;
            float padY = 3.f * dpi;
            float bw = bm.width + padX * 2.f;
            float bh = bm.height + padY * 2.f;
            float bx = sm + 6.f * dpi;
            float by = sm + 6.f * dpi;
            core::RectF badgeRect = core::RectF::fromXYWH(bx, by, bw, bh);
            ctx.fillRoundedRect(badgeRect, 4.f * dpi, core::ColorRgba::fromRgbaHex(0x1E1E1EE0));
            ctx.strokeRoundedRect(badgeRect, 4.f * dpi, core::ColorRgba::fromRgbaHex(0xFFFFFF40), 1.f * dpi);
            ctx.drawText(bx + padX, by + padY, badgeLayout, core::ColorRgba::fromHex(0xFFFFFF));
        }
    }

    if (showScaleTip_) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(scale_ * 100.f)));
        gfx::TextLayout tl(buf, 10.f * dpi);
        auto tm = tl.metrics();
        float tw = tm.width + 16.f * dpi;
        float th = tm.height + 10.f * dpi;
        float tx = sm + (cw - tw) * 0.5f;
        float ty = sm + (ch - th) * 0.5f;
        ctx.fillRoundedRect(core::RectF::fromXYWH(tx, ty, tw, th), 5.f * dpi,
                            core::ColorRgba::fromRgbaHex(0x1E1E1EDD));
        ctx.drawText(tx + 8.f * dpi, ty + 5.f * dpi, tl, core::ColorRgba::fromHex(0xFFFFFF));
    }
}

core::PointF PinWindow::toImgPos(const core::PointF& pos) const {
    float sm = shadowMargin();
    return {(pos.x - sm) / scale_, (pos.y - sm) / scale_};
}

void PinWindow::onMouseDown(const core::PointF& screenPos, bool right) {
    grabKeyboardFocus();
    if (right) {
        if (inPlaceAnnotate_) {
            close();
            return;
        }
        if (toolMain_) {
            if (toolMain_->isVisible()) {
                toolMain_->cancelSelect();
                toolMain_->hide();
                if (toolSub_) toolSub_->hideTools();
            } else {
                layoutTools();
                toolMain_->show();
                toolMain_->raise();
                updateToolSubState();
            }
        }
        return;
    }

    int64_t t = nowMs();
    if (!editingText_ &&
        (t - lastClickTime_ <= 320) &&
        std::hypot(screenPos.x - lastClickPos_.x, screenPos.y - lastClickPos_.y) <= 6.f) {
        if (newShape_) {
            history_->undo();
            newShape_ = nullptr;
        }
        copyToClipboard();
        return;
    }
    lastClickTime_ = t;
    lastClickPos_ = screenPos;

    auto pos = toImgPos(screenPos);
    isMouseDown_ = true;
    hasDragged_ = false;
    pressPos_ = screenPos;

    std::string tool = toolMain_ ? toolMain_->currentTool() : std::string{};
    if (tool.empty()) {
        if (editingText_) {
            editingText_->finishEdit();
            refresh();
        }
        if (!inPlaceAnnotate_) {
            dragging_ = true;
            auto pt = sc::platform::X11Display::get()->pointerPosition();
            dragStartMouseX_ = pt.x;
            dragStartMouseY_ = pt.y;
            dragStartWindowX_ = x;
            dragStartWindowY_ = y;
            if (GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default())) {
                gdk_seat_grab(seat, gtk_widget_get_window(native()),
                              GDK_SEAT_CAPABILITY_POINTER, FALSE, nullptr, nullptr,
                              nullptr, nullptr);
            }
        }
        return;
    }

    if (shapeHover_) {
        newShape_ = nullptr;
        shapeHover_->mouseDown(pos.x, pos.y);
    } else {
        if (editingText_) {
            editingText_->finishEdit();
        }
        shapeHover_ = history_->createShape(tool, pos.x, pos.y);
        newShape_ = shapeHover_;
    }
    refresh();
}

void PinWindow::onMouseMove(const core::PointF& screenPos) {
    if (dragging_) {
        hasDragged_ = true;
        auto pt = sc::platform::X11Display::get()->pointerPosition();
        int nx = dragStartWindowX_ + (pt.x - dragStartMouseX_);
        int ny = dragStartWindowY_ + (pt.y - dragStartMouseY_);
        if (nx != x || ny != y) {
            move(nx, ny);
            layoutTools();
        }
        return;
    }

    auto pos = toImgPos(screenPos);
    if (isMouseDown_) {
        if (std::hypot(screenPos.x - pressPos_.x, screenPos.y - pressPos_.y) > 2.f)
            hasDragged_ = true;
        if (shapeHover_) {
            shapeHover_->mouseDrag(pos.x, pos.y);
            shapeHover_->setCursor();
            refresh();
        }
        return;
    }

    sc::shapes::ShapeBase* foundHover = nullptr;
    for (auto it = history_->shapes.rbegin(); it != history_->shapes.rend(); ++it) {
        if ((*it)->isUndo) continue;
        (*it)->mouseMove(pos.x, pos.y);
        if (!foundHover && (*it)->hoverDraggerIndex >= 0) {
            foundHover = it->get();
        }
    }
    shapeHover_ = foundHover;
    if (shapeHover_) {
        shapeHover_->setCursor();
    } else {
        std::string tool = toolMain_ ? toolMain_->currentTool() : std::string{};
        if (tool == "text") setCursorByHint(2);
        else if (!tool.empty()) setCursorByHint(3);
        else setCursorByHint(inPlaceAnnotate_ ? 0 : 1);
    }
    refresh();
}

void PinWindow::onMouseUp(const core::PointF& screenPos, bool right) {
    if (right) return;
    if (dragging_) {
        dragging_ = false;
        if (GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default())) {
            gdk_seat_ungrab(seat);
        }
    }
    isMouseDown_ = false;
    auto* justCreated = newShape_;
    newShape_ = nullptr;
    auto pos = toImgPos(screenPos);
    if (shapeHover_) {
        if (shapeHover_ == justCreated && !hasDragged_ && !shapeHover_->isValidWithoutDrag()) {
            history_->removeShape(shapeHover_);
            shapeHover_ = nullptr;
        } else {
            shapeHover_->mouseUp(pos.x, pos.y);
            shapeHover_->setCursor();
        }
    }
    refresh();
    setTimer(500, kHoverTimer);
}

void PinWindow::onMouseWheel(const core::PointF& pos, int delta) {
    if (!inPlaceAnnotate_ && sc::platform::X11Display::get()->controlDown()) {
        float factor = delta > 0 ? 1.1f : 1.f / 1.1f;
        float next = scale_ * factor;
        int maxDim = std::max(screenImage_->width(), screenImage_->height());
        float maxScale = std::max(1.f, std::min(8.f, 16000.f / maxDim));
        scale_ = std::clamp(next, 0.1f, maxScale);
        float sm = shadowMargin();
        uint32_t newW = static_cast<uint32_t>(std::max(1.f, std::round(screenImage_->width() * scale_ + 2.f * sm)));
        uint32_t newH = static_cast<uint32_t>(std::max(1.f, std::round(screenImage_->height() * scale_ + 2.f * sm)));
        resize(newW, newH);
        layoutTools();
        showScaleTip_ = true;
        setTimer(1200, kScaleTipTimer);
        refresh();
    } else if (shapeHover_) {
        auto imgPos = toImgPos(pos);
        shapeHover_->mouseWheel(imgPos.x, imgPos.y, delta);
        if (toolSub_ && toolSub_->hasContent() && toolMain_) {
            toolSub_->syncSliderFromWheel(toolMain_->currentTool(),
                                          sliderValue(toolMain_->currentTool()));
        }
        refresh();
    }
}

void PinWindow::onTextInput(const std::string& utf8) {
    if (!editingText_ || utf8.empty()) return;
    editingText_->text.append(utf8);
    editingText_->applyStyle();
    refresh();
}

void PinWindow::onKeyDown(uint32_t key) {
    bool ctrl = sc::platform::X11Display::get()->controlDown();

    if (editingText_ && !ctrl) {
        if (key == GDK_KEY_Escape) {
            editingText_->finishEdit();
            refresh();
            return;
        }
        if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) {
            editingText_->text.push_back('\n');
            editingText_->applyStyle();
            refresh();
            return;
        }
        if (key == GDK_KEY_BackSpace) {
            auto& t = editingText_->text;
            while (!t.empty()) {
                unsigned char c = static_cast<unsigned char>(t.back());
                t.pop_back();
                if ((c & 0xC0) != 0x80) break;
            }
            editingText_->applyStyle();
            refresh();
            return;
        }
        uint32_t uc = gdk_keyval_to_unicode(key);
        if (uc >= 32 && uc != 127) {
            char utf8[8] = {0};
            int len = g_unichar_to_utf8(uc, utf8);
            if (len > 0) {
                editingText_->text.append(utf8, static_cast<size_t>(len));
                editingText_->applyStyle();
                refresh();
                return;
            }
        }
    }

    if (key == GDK_KEY_Escape) { close(); return; }
    if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) {
        copyToClipboard();
        return;
    }
    if (key == GDK_KEY_Delete && shapeHover_) {
        history_->removeHoverShape(shapeHover_);
        shapeHover_ = nullptr;
        refresh();
        return;
    }
    if (ctrl && (key == GDK_KEY_z || key == GDK_KEY_Z)) {
        shapeHover_ = nullptr;
        history_->undo();
        refresh();
        return;
    }
    if (ctrl && (key == GDK_KEY_y || key == GDK_KEY_Y)) {
        shapeHover_ = nullptr;
        history_->redo();
        refresh();
        return;
    }
    if (ctrl && (key == GDK_KEY_c || key == GDK_KEY_C)) {
        copyToClipboard();
        return;
    }
    if (ctrl && (key == GDK_KEY_s || key == GDK_KEY_S)) {
        saveToFile();
        return;
    }
}

void PinWindow::onTimer(uint32_t id) {
    if (id == kHoverTimer) {
        if (editingText_) refresh();
    } else if (id == kScaleTipTimer) {
        showScaleTip_ = false;
        killTimer(kScaleTipTimer);
        refresh();
    }
}

void PinWindow::onShown() {
    raise();
    if (toolMain_) toolMain_->raise();
    if (toolSub_ && toolSub_->isVisible()) toolSub_->raise();
    grabKeyboardFocus();
}

void PinWindow::onDestroy() {
    if (dragging_) {
        dragging_ = false;
        if (GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default())) {
            gdk_seat_ungrab(seat);
        }
    }
    killTimer(kHoverTimer);
    killTimer(kScaleTipTimer);
    if (editingText_) editingText_->finishEdit();
    shapeHover_ = nullptr;
    newShape_ = nullptr;
    if (toolSub_) { toolSub_->close(); toolSub_.reset(); }
    if (toolMain_) { toolMain_->close(); toolMain_.reset(); }

    bool wasInPlace = inPlaceAnnotate_;
    inPlaceAnnotate_ = false;
    if (wasInPlace) {
        CaptureWindow::closeAll();
    }

    auto* self = this;
    sc::core::AppContext::get()->postTask([self]() {
        auto it = std::find_if(pinWindows.begin(), pinWindows.end(),
            [self](const std::unique_ptr<PinWindow>& p) { return p.get() == self; });
        if (it != pinWindows.end()) pinWindows.erase(it);
        if (pinWindows.empty() && !CaptureWindow::hasWindow() &&
            sc::core::AppContext::get()->autoQuit()) {
            g_timeout_add(180, +[](gpointer) -> gboolean {
                sc::core::AppContext::get()->quit(0);
                return G_SOURCE_REMOVE;
            }, nullptr);
        }
    });
}

void PinWindow::layoutTools() {
    if (!toolMain_) return;
    auto mon = sc::platform::X11Display::get()->monitorAt(x, y);
    float gap = 5.f * dpi;
    float subGap = ToolSub::kMainGap * dpi;
    float mainH = toolMain_->height;
    float subH = toolSub_ ? toolSub_->desiredHeightPx() : 0.f;
    float groupH = mainH + subGap + subH;
    bool showSub = toolSub_ && !toolMain_->currentTool().empty() && toolSub_->hasContent();
    float usedH = showSub ? groupH : mainH;

    float sm = shadowMargin();
    float contentX = static_cast<float>(x) + sm;
    float contentY = static_cast<float>(y) + sm;
    float contentW = width - 2.f * sm;
    float contentH = height - 2.f * sm;

    int ty = static_cast<int>(std::round(contentY + contentH + sm + gap));
    if (ty + static_cast<int>(groupH) > mon.bottom) {
        if (contentY - sm - gap - groupH >= mon.top) {
            ty = static_cast<int>(std::round(contentY - sm - gap - usedH));
        } else {
            ty = static_cast<int>(std::round(contentY + contentH - gap - usedH));
        }
    }
    int tx = static_cast<int>(std::round(contentX + contentW - toolMain_->width));
    tx = std::max(mon.left, std::min(tx, mon.right - static_cast<int>(toolMain_->width)));
    ty = std::max(mon.top, std::min(ty, mon.bottom - static_cast<int>(usedH)));

    toolMain_->move(tx, ty);
    toolMain_->raise();

    if (showSub) {
        toolSub_->updatePosition(mon, tx, ty, toolMain_->width, toolMain_->height,
                                 toolMain_->getSelectedBtnCenterX());
        toolSub_->raise();
    } else if (toolSub_) {
        toolSub_->hideTools();
    }
}

void PinWindow::updateToolSubState() {
    if (!toolSub_ || !toolMain_) return;
    std::string tool = toolMain_->currentTool();
    if (tool.empty() || !toolMain_->isVisible()) {
        toolSub_->hideTools();
        layoutTools();
        return;
    }
    toolSub_->showForTool(tool);
    layoutTools();
}

std::shared_ptr<gfx::ImageSurface> PinWindow::getImagePixels() const {
    if (!screenImage_) return nullptr;
    auto result = std::make_shared<gfx::ImageSurface>(screenImage_->width(), screenImage_->height());
    auto ctx = gfx::CanvasContext::fromSurface(*result);
    ctx->drawImage(*screenImage_,
                   {0, 0, static_cast<float>(screenImage_->width()),
                    static_cast<float>(screenImage_->height())});
    for (auto& shape : history_->shapes) {
        if (!shape->isUndo) shape->paint(*ctx);
    }
    return result;
}

core::ColorRgba PinWindow::selectedColor() const {
    int idx = sc::core::ConfigManager::get()->getInt("toolPin", "colorIndex", 0);
    static const uint32_t colors[] = {0xCF1322FF, 0xD48806FF, 0x389E0DFF, 0x13C2C2FF,
        0x0958D9FF, 0x722ED1FF, 0xEB2F96FF, 0x000000FF, 0xFFFFFFFF};
    idx = std::clamp(idx, 0, 8);
    return core::ColorRgba::fromRgbaHex(colors[idx]);
}

float PinWindow::sliderValue(const std::string& tool) const {
    static const std::map<std::string, std::pair<std::string, float>> cfg = {
        {"rect",{"width",2.f}},{"ellipse",{"width",2.f}},{"arrow",{"width",3.f}},
        {"number",{"radius",16.f}},{"line",{"width",12.f}},{"pen",{"width",12.f}},
        {"text",{"fontSize",20.f}},{"mosaic",{"width",28.f}},{"eraser",{"width",28.f}}};
    auto it = cfg.find(tool);
    if (it == cfg.end()) return 2.f * dpi;
    float logical = sc::core::ConfigManager::get()->getToolFloat(tool, it->second.first, it->second.second);
    return logical * dpi;
}

float PinWindow::clampSliderValue(const std::string& tool, float physicalPx) {
    static const std::map<std::string, std::pair<std::string, std::pair<float,float>>> cfg = {
        {"rect",{"width",{1,26}}},{"ellipse",{"width",{1,26}}},{"arrow",{"width",{1,16}}},
        {"number",{"radius",{6,86}}},{"line",{"width",{1,60}}},{"pen",{"width",{1,60}}},
        {"text",{"fontSize",{10,60}}},{"mosaic",{"width",{18,68}}},{"eraser",{"width",{18,68}}}};
    auto it = cfg.find(tool);
    if (it == cfg.end()) return physicalPx;
    float logical = std::clamp(physicalPx / dpi, it->second.second.first, it->second.second.second);
    sc::core::ConfigManager::get()->setToolFloat(tool, it->second.first, logical);
    return logical * dpi;
}

bool PinWindow::toolFlag(const std::string& tool, const std::string& key) const {
    bool def = false;
    if (key == "fill") def = (tool == "arrow" || tool == "number");
    return sc::core::ConfigManager::get()->getToolBool(tool, key, def);
}

bool PinWindow::shiftDown() const { return sc::platform::X11Display::get()->shiftDown(); }

void PinWindow::beginTextEdit(sc::shapes::ShapeText* shape) {
    editingText_ = shape;
    grabKeyboardFocus();
}

void PinWindow::endTextEdit() { editingText_ = nullptr; }
void PinWindow::setCursorHint(int hint) { BaseWindow::setCursorByHint(hint); }
void PinWindow::postTask(std::function<void()> fn) {
    sc::core::AppContext::get()->postTask(std::move(fn));
}

void PinWindow::saveToFile() {
    if (editingText_) editingText_->finishEdit();
    auto img = getImagePixels();
    if (!img) return;
    if (toolMain_) toolMain_->hide();
    if (toolSub_) toolSub_->hide();
    hide();
    if (inPlaceAnnotate_) CaptureWindow::setOverlaysVisible(false);

    // Hand the shot to the shared save dialog; it tears down this window and
    // the capture overlays when the user picks an action.
    sc::platform::x11ErrorTrapPush();
    SaveDialogWindow::open(std::move(img), sc::core::AppContext::get()->autoQuit());
    sc::platform::x11ErrorTrapPopIgnored();
}

void PinWindow::copyToClipboard() {
    if (editingText_) editingText_->finishEdit();
    auto img = getImagePixels();
    if (img) sc::platform::ClipboardService::setImage(img);
    close();
}

} // namespace sc::windows
