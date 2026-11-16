#include "screencapture/windows/pin_window.h"
#include "screencapture/windows/capture_window.h"
#include "screencapture/windows/save_dialog.h"
#include "screencapture/windows/tool_bars.h"
#include "screencapture/core/app_context.h"
#include "screencapture/core/config_manager.h"
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
    raw->create("ScreenCapture Pin",
                core::RectF::fromXYWH(static_cast<float>(x), static_cast<float>(y),
                                      static_cast<float>(image->width()),
                                      static_cast<float>(image->height())),
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
    }

    layoutTools();
}

void PinWindow::detachToDesktopPin() {
    if (inPlaceAnnotate_) {
        inPlaceAnnotate_ = false;
        if (toolMain_) toolMain_->cancelSelect();
        updateToolSubState();
        CaptureWindow::closeAll();
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
    ctx.save();
    ctx.scale(scale_, scale_);
    ctx.drawImage(*screenImage_,
                  {0, 0, static_cast<float>(screenImage_->width()),
                   static_cast<float>(screenImage_->height())});
    for (auto& shape : history_->shapes) {
        if (!shape->isUndo) shape->paint(ctx);
    }
    if (!isMouseDown_ && shapeHover_) shapeHover_->paintDragger(ctx);
    ctx.restore();
    ctx.strokeRect({0, 0, width, height}, core::ColorRgba::fromHex(0x1677FF), 2.f * dpi);

    if (showScaleTip_) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(scale_ * 100.f)));
        gfx::TextLayout tl(buf, 10.f * dpi);
        auto tm = tl.metrics();
        float tw = tm.width + 16.f * dpi;
        float th = tm.height + 10.f * dpi;
        float tx = (width - tw) * 0.5f;
        float ty = (height - th) * 0.5f;
        ctx.fillRoundedRect(core::RectF::fromXYWH(tx, ty, tw, th), 5.f * dpi,
                            core::ColorRgba::fromRgbaHex(0x1E1E1EDD));
        ctx.drawText(tx + 8.f * dpi, ty + 5.f * dpi, tl, core::ColorRgba::fromHex(0xFFFFFF));
    }
}

core::PointF PinWindow::toImgPos(const core::PointF& pos) const {
    return {pos.x / scale_, pos.y / scale_};
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
            dragStartWindowX_ = x;
            dragStartWindowY_ = y;
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
    auto pos = toImgPos(screenPos);
    if (isMouseDown_) {
        if (std::hypot(screenPos.x - pressPos_.x, screenPos.y - pressPos_.y) > 2.f)
            hasDragged_ = true;
        if (shapeHover_) {
            shapeHover_->mouseDrag(pos.x, pos.y);
            shapeHover_->setCursor();
            refresh();
        } else {
            std::string tool = toolMain_ ? toolMain_->currentTool() : std::string{};
            if (tool.empty() && !inPlaceAnnotate_) {
                float dx = screenPos.x - pressPos_.x;
                float dy = screenPos.y - pressPos_.y;
                int nx = dragStartWindowX_ + static_cast<int>(dx);
                int ny = dragStartWindowY_ + static_cast<int>(dy);
                move(nx, ny);
                layoutTools();
            }
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
        resize(static_cast<uint32_t>(screenImage_->width() * scale_),
               static_cast<uint32_t>(screenImage_->height() * scale_));
        layoutTools();
        showScaleTip_ = true;
        setTimer(1200, kScaleTipTimer);
        refresh();
    } else if (shapeHover_) {
        shapeHover_->mouseWheel(pos.x / scale_, pos.y / scale_, delta);
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

    int ty = y + static_cast<int>(height) + static_cast<int>(gap);
    if (ty + static_cast<int>(groupH) > mon.bottom) {
        if (y - static_cast<int>(gap) - static_cast<int>(groupH) >= mon.top) {
            ty = y - static_cast<int>(gap) - static_cast<int>(usedH);
        } else {
            ty = y + static_cast<int>(height) - static_cast<int>(gap) - static_cast<int>(usedH);
        }
    }
    int tx = x + static_cast<int>(width - toolMain_->width);
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
