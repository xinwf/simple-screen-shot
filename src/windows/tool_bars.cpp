#include "screencapture/windows/tool_bars.h"
#include "screencapture/core/config_manager.h"
#include "screencapture/core/i18n.h"
#include <algorithm>
#include <cmath>

namespace sc::windows {

using namespace sc::ui;

namespace {
struct SliderRule {
    const char* tool;
    const char* key;
    float minVal;
    float maxVal;
    float defVal;
};

constexpr SliderRule kSliderRules[] = {
    {"rect",    "width",    1.f, 26.f,  2.f},
    {"ellipse", "width",    1.f, 26.f,  2.f},
    {"arrow",   "width",    1.f, 16.f,  3.f},
    {"number",  "radius",   6.f, 86.f, 16.f},
    {"line",    "width",    1.f, 60.f, 12.f},
    {"text",    "fontSize", 10.f, 60.f, 20.f},
    {"mosaic",  "width",   18.f, 68.f, 28.f},
    {"eraser",  "width",   18.f, 68.f, 28.f},
};

const SliderRule* findSliderRule(const std::string& tool) {
    for (const auto& r : kSliderRules) {
        if (tool == r.tool) return &r;
    }
    return nullptr;
}

constexpr uint32_t kPaletteColors[9] = {
    0xCF1322FF, 0xD48806FF, 0x389E0DFF, 0x13C2C2FF,
    0x0958D9FF, 0x722ED1FF, 0xEB2F96FF, 0x000000FF, 0xFFFFFFFF
};

const char* const kPaletteNames[9] = {
    "color.red", "color.yellow", "color.green", "color.cyan",
    "color.blue", "color.purple", "color.pink", "color.black", "color.white"
};
} // namespace

bool ToolBarBase::tryHandleScreenClick(int screenX, int screenY) {
    if (!isVisible() || !body) return false;
    float lx = static_cast<float>(screenX - x);
    float ly = static_cast<float>(screenY - y);
    if (lx < 0.f || ly < 0.f || lx > width || ly > height) return false;
    WidgetNode* hit = body->hitTest(lx, ly);
    while (hit && !dynamic_cast<Button*>(hit)) {
        hit = hit->parent;
    }
    if (auto* btn = dynamic_cast<Button*>(hit)) {
        btn->onClick.emit(btn);
        return true;
    }
    return false;
}

Button* ToolBarBase::addButton(const std::string& id, const std::string& glyph,
                               const std::string& tip, float btnWidth, bool iconFont) {
    auto* b = body->makeChild<Button>();
    b->id = id;
    b->setText(glyph);
    b->setFontSize(13.f);
    b->setSize(btnWidth, 30.f);
    b->setJustifyContent(Justify::Center);
    b->setAlignItems(Align::Center);
    b->setFontFamily(iconFont ? "icon" : "Sans");
    b->setColor(0x222222FF);
    b->hoverBgColor = 0xF2F2F2FF;

    std::string tooltipText = tip;
    if (tooltipText.empty()) {
        tooltipText = sc::core::I18n::get()->get("cap." + id);
        if (tooltipText == "cap." + id) tooltipText = sc::core::I18n::get()->get("tool." + id);
        if (tooltipText == "tool." + id) tooltipText = id;
    }
    b->tooltip = tooltipText;
    b->onClick.add([this, id](Button*) {
        if (onAction) onAction(id);
    });
    return b;
}

void ToolBarBase::addSeparator(float sepWidth, uint32_t color) {
    auto* s = body->makeChild<WidgetNode>();
    s->setSize(sepWidth, 18.f);
    s->setBg(color);
}

void ToolBarBase::buildBar(const std::vector<std::string>& ids,
                           const std::vector<std::string>& glyphs) {
    body->children.clear();
    body->setBg(0xFFFFFFFF);
    body->setBorder(1.f, 0xA8A8A8FF);
    body->setFlexDirection(FlexDirection::Row);
    body->setAlignItems(Align::Center);
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == "|" || ids[i] == "spliter") {
            addSeparator();
            continue;
        }
        addButton(ids[i], i < glyphs.size() ? glyphs[i] : ids[i]);
    }
}

// =============================================================================
// ToolCap
// =============================================================================
void ToolCap::onCreated() {
    static const std::vector<std::string> ids = {
        "mark", "long", "video", "ocr", "qrcode", "pin", "spliter", "close", "save", "clipboard"
    };
    static const std::vector<std::string> glyphs = {
        "\uE97F", "\uE73E", "\uE660", "\uE67B", "\uE71E", "\uE6A2", "",
        "\uE62D", "\uE608", "\uE6AD"
    };
    buildBar(ids, glyphs);
    resize(static_cast<uint32_t>(std::lround((9.f * 32.f + 1.f) * dpi)),
           static_cast<uint32_t>(std::lround(32.f * dpi)));
}

// =============================================================================
// ToolMain
// =============================================================================
void ToolMain::onCreated() {
    static const std::vector<std::string> ids = {
        "rect", "ellipse", "arrow", "number", "line", "text", "mosaic", "eraser",
        "|", "undo", "redo", "|", "pin", "close", "save", "clipboard"
    };
    static const std::vector<std::string> glyphs = {
        "\uE8E8", "\uE6BC", "\uE603", "\uE776", "\uE601", "\uE6EC", "\uE82E", "\uE6BE",
        "|", "\uED85", "\uED8A", "|",
        "\uE6A2", "\uE62D", "\uE608", "\uE6AD"
    };
    buildBar(ids, glyphs);
    resize(static_cast<uint32_t>(std::lround((14.f * 32.f + 2.f) * dpi)),
           static_cast<uint32_t>(std::lround(32.f * dpi)));
}

void ToolMain::selectTool(const std::string& id) {
    currentTool_ = (currentTool_ == id) ? std::string{} : id;
    for (auto& child : body->children) {
        if (auto* b = dynamic_cast<Button*>(child.get())) {
            if (!currentTool_.empty() && b->id == currentTool_) {
                b->setBg(0xE6F4FFFF);
                b->hoverBgColor = 0xE6F4FFFF;
                b->setColor(0x1677FFFF);
            } else {
                b->setBg(0x00000000);
                b->hoverBgColor = 0xF2F2F2FF;
                b->setColor(0x222222FF);
            }
        }
    }
    refresh();
}

void ToolMain::cancelSelect() {
    if (!currentTool_.empty()) {
        selectTool(currentTool_);
    }
}

float ToolMain::getSelectedBtnCenterX() const {
    if (!body || currentTool_.empty()) return width * 0.5f;
    for (const auto& child : body->children) {
        if (auto* b = dynamic_cast<const Button*>(child.get())) {
            if (b->id == currentTool_) {
                return b->x + b->width * 0.5f;
            }
        }
    }
    return width * 0.5f;
}

// =============================================================================
// ToolSub (Secondary Style Bar for Annotation Tools)
// =============================================================================
void ToolSub::onCreated() {
    body->setBg(0x00000000);
    contentNode_ = body->makeChild<WidgetNode>();
    contentNode_->setPosition(Edge::Left, 1.f);
    contentNode_->setPosition(Edge::Top, 4.f);
    contentNode_->setPosition(Edge::Right, 1.f);
    contentNode_->setPosition(Edge::Bottom, 1.f);
    contentNode_->setFlexDirection(FlexDirection::Row);
    contentNode_->setAlignItems(Align::Center);
    resize(static_cast<uint32_t>(std::lround(372.f * dpi)),
           static_cast<uint32_t>(desiredHeightPx()));
}

void ToolSub::onPaint(sc::gfx::CanvasContext& ctx) {
    if (!hasTools_) return;
    ctx.clear({0.f, 0.f, 0.f, 0.f});
    float bw = 1.f * dpi;
    float half = bw * 0.5f;
    float top = std::floor(3.f * dpi) + half;
    float right = std::floor(width) - half;
    float bottom = std::floor(height) - half;
    float ax = std::clamp(arrowX_, 8.f * dpi, std::max(8.f * dpi, right - 8.f * dpi));

    sc::gfx::PathBuilder borderPath;
    borderPath.moveTo(half, top);
    borderPath.lineTo(ax - 3.5f * dpi, top);
    borderPath.lineTo(ax, half);
    borderPath.lineTo(ax + 3.5f * dpi, top);
    borderPath.lineTo(right, top);
    borderPath.lineTo(right, bottom);
    borderPath.lineTo(half, bottom);
    borderPath.close();

    ctx.fillPath(borderPath, core::ColorRgba::fromHex(0xFFFFFF));
    ctx.strokePath(borderPath, core::ColorRgba::fromHex(0xA8A8A8), bw);
}

void ToolSub::showForTool(const std::string& toolId) {
    const auto* rule = findSliderRule(toolId);
    if (!rule) {
        hideTools();
        return;
    }
    curToolId_ = toolId;
    curSliderKey_ = rule->key;
    sliderMin_ = rule->minVal;
    sliderMax_ = rule->maxVal;
    auto* cfg = sc::core::ConfigManager::get();
    sliderVal_ = std::clamp(cfg->getToolFloat(toolId, curSliderKey_, rule->defVal),
                            sliderMin_, sliderMax_);
    selectColorIndex_ = std::clamp(cfg->getInt("toolPin", "colorIndex", 0), 0, 8);
    hasTools_ = true;
    rebuildControls();
}

void ToolSub::rebuildControls() {
    if (!contentNode_) return;
    contentNode_->removeAllChildren();
    colorBtns_.clear();
    slider_ = nullptr;

    auto* cfg = sc::core::ConfigManager::get();
    auto* i18n = sc::core::I18n::get();

    auto makeToggle = [&](const std::string& glyph, const std::string& cfgKey,
                          bool defVal, const std::string& tipKey) {
        bool current = cfg->getToolBool(curToolId_, cfgKey, defVal);
        auto* btn = contentNode_->makeChild<Button>();
        btn->setText(glyph);
        btn->setSize(32.f, 29.f);
        btn->setJustifyContent(Justify::Center);
        btn->setAlignItems(Align::Center);
        btn->setFontFamily("icon");
        btn->setFontSize(13.f);
        btn->tooltip = i18n->get(tipKey);
        auto applyStyle = [](Button* b, bool active) {
            if (active) {
                b->setBg(0xE6F4FFFF);
                b->hoverBgColor = 0xE6F4FFFF;
                b->setColor(0x1677FFFF);
            } else {
                b->setBg(0x00000000);
                b->hoverBgColor = 0xF2F2F2FF;
                b->setColor(0x222222FF);
            }
        };
        applyStyle(btn, current);
        std::string toolCopy = curToolId_;
        btn->onClick.add([this, toolCopy, cfgKey, defVal, applyStyle](Button* b) {
            auto* cm = sc::core::ConfigManager::get();
            bool next = !cm->getToolBool(toolCopy, cfgKey, defVal);
            cm->setToolBool(toolCopy, cfgKey, next);
            applyStyle(b, next);
            refresh();
            if (onStyleChanged) onStyleChanged();
        });
    };

    int btnCount = 1;
    bool withColors = true;
    centerOnBtn_ = false;

    if (curToolId_ == "rect") {
        makeToggle("\uE602", "fill", false, "tool.rectFill");
    } else if (curToolId_ == "ellipse") {
        makeToggle("\uE600", "fill", false, "tool.ellipseFill");
    } else if (curToolId_ == "arrow") {
        makeToggle("\uE604", "fill", true, "tool.arrowFill");
    } else if (curToolId_ == "number") {
        makeToggle("\uE605", "fill", true, "tool.numberFill");
    } else if (curToolId_ == "line") {
        makeToggle("\uE607", "semiTransparent", false, "tool.semiTransparent");
    } else if (curToolId_ == "text") {
        btnCount = 2;
        makeToggle("\uE634", "bold", false, "tool.bold");
        makeToggle("\uE682", "italic", false, "tool.italic");
    } else if (curToolId_ == "mosaic" || curToolId_ == "eraser") {
        withColors = false;
        centerOnBtn_ = true;
        makeToggle("\uE602", "rect", false, "tool.rectFill");
    }

    // Width / Radius / FontSize Slider
    slider_ = contentNode_->makeChild<Slider>();
    slider_->setSize(80.f, 29.f);
    slider_->setMargin(3.f, 0.f, 3.f, 0.f);
    slider_->setRange(sliderMin_, sliderMax_);
    slider_->setValue(sliderVal_);
    slider_->onValueChanged.add([this](Slider*, float val) {
        sliderVal_ = val;
        sc::core::ConfigManager::get()->setToolFloat(curToolId_, curSliderKey_, val);
        if (onStyleChanged) onStyleChanged();
    });

    if (withColors) {
        for (int i = 0; i < 9; ++i) {
            auto* cBtn = contentNode_->makeChild<Button>();
            cBtn->setSize(28.f, 29.f);
            cBtn->setJustifyContent(Justify::Center);
            cBtn->setAlignItems(Align::Center);
            cBtn->hoverBgColor = 0xF2F2F2FF;
            cBtn->tooltip = i18n->get(kPaletteNames[i]);
            colorBtns_.push_back(cBtn);

            auto* swatch = cBtn->makeChild<Label>();
            swatch->setSize(14.f, 14.f);
            swatch->setJustifyContent(Justify::Center);
            swatch->setAlignItems(Align::Center);
            swatch->setFontFamily("icon");
            swatch->setFontSize(8.f);
            swatch->setBg(kPaletteColors[i]);
            swatch->setBorderRadius(2.f);
            if (i == 8) {
                swatch->setColor(0x000000FF);
                swatch->setBorder(1.f, 0xA8A8A8FF);
            } else {
                swatch->setColor(0xFFFFFFFF);
            }
            if (i == selectColorIndex_) {
                swatch->setText("\uE6AD");
            }

            cBtn->onClick.add([this, i](Button*) {
                if (selectColorIndex_ != i) {
                    if (selectColorIndex_ >= 0 && selectColorIndex_ < static_cast<int>(colorBtns_.size())) {
                        if (!colorBtns_[selectColorIndex_]->children.empty()) {
                            if (auto* oldLbl = dynamic_cast<Label*>(colorBtns_[selectColorIndex_]->children[0].get())) {
                                oldLbl->setText("");
                            }
                        }
                    }
                    selectColorIndex_ = i;
                    if (!colorBtns_[i]->children.empty()) {
                        if (auto* newLbl = dynamic_cast<Label*>(colorBtns_[i]->children[0].get())) {
                            newLbl->setText("\uE6AD");
                        }
                    }
                    sc::core::ConfigManager::get()->setInt("toolPin", "colorIndex", i);
                    refresh();
                }
                if (onStyleChanged) onStyleChanged();
            });
        }
    }

    float logicW = btnCount * 32.f + 86.f + (withColors ? 9 * 28.f : 0.f) + 2.f;
    resize(static_cast<uint32_t>(std::lround(logicW * dpi)),
           static_cast<uint32_t>(desiredHeightPx()));
}

void ToolSub::hideTools() {
    hasTools_ = false;
    curToolId_.clear();
    hide();
}

void ToolSub::updatePosition(const core::RectI& workArea, int mainX, int mainY,
                             float /*mainW*/, float mainH, float btnCenterX) {
    if (!hasTools_) return;
    int px = centerOnBtn_
                 ? static_cast<int>(std::lround(mainX + btnCenterX - width * 0.5f))
                 : mainX;
    int py = mainY + static_cast<int>(std::lround(mainH + kMainGap * dpi));
    int maxX = std::max(workArea.left, workArea.right - static_cast<int>(width));
    int finalX = std::clamp(px, workArea.left, maxX);
    arrowX_ = static_cast<float>(mainX) + btnCenterX - static_cast<float>(finalX);

    relocateWindow(finalX, py, static_cast<int>(width), static_cast<int>(height));
    show();
    raise();
}

float ToolSub::syncSliderFromWheel(const std::string& toolId, float physicalPx) {
    const auto* rule = findSliderRule(toolId);
    if (!rule) return physicalPx;
    float logical = std::clamp(std::round(physicalPx / dpi), rule->minVal, rule->maxVal);
    sc::core::ConfigManager::get()->setToolFloat(toolId, rule->key, logical);
    if (curToolId_ == toolId && slider_) {
        sliderVal_ = logical;
        slider_->setValue(logical);
        refresh();
    }
    return logical * dpi;
}

// =============================================================================
// ToolLong
// =============================================================================
void ToolLong::onCreated() {
    static const std::vector<std::string> ids = {"pin", "close", "save", "clipboard"};
    static const std::vector<std::string> glyphs = {
        "\uE6A2", "\uE62D", "\uE608", "\uE6AD"
    };
    buildBar(ids, glyphs);
    resize(static_cast<uint32_t>(std::lround(4.f * 32.f * dpi)),
           static_cast<uint32_t>(std::lround(32.f * dpi)));
}

// =============================================================================
// ToolVideo
// =============================================================================
void ToolVideo::onCreated() {
    rebuildSettingBar();
}

void ToolVideo::rebuildSettingBar() {
    recording_ = false;
    timerLabel_ = nullptr;
    body->children.clear();
    body->setBg(0xFFFFFFFF);
    body->setBorder(1.f, 0xA8A8A8FF);
    body->setFlexDirection(FlexDirection::Row);
    body->setAlignItems(Align::Center);

    btnMp4_ = addButton("mp4", "MP4", sc::core::I18n::get()->get("video.outputMp4"), 42.f, false);
    btnMp4_->onClick.clear();
    btnMp4_->onClick.add([this](Button*) {
        formatIndex_ = 0;
        updateToggleStyles();
    });

    btnGif_ = addButton("gif", "GIF", sc::core::I18n::get()->get("video.outputGif"), 42.f, false);
    btnGif_->onClick.clear();
    btnGif_->onClick.add([this](Button*) {
        formatIndex_ = 1;
        useSpeaker_ = false;
        useMic_ = false;
        updateToggleStyles();
    });

    addSeparator();

    btnSpeaker_ = addButton("speaker", "\uE654", sc::core::I18n::get()->get("video.recordSystem"));
    btnSpeaker_->onClick.clear();
    btnSpeaker_->onClick.add([this](Button*) {
        if (formatIndex_ != 0) return;
        useSpeaker_ = !useSpeaker_;
        updateToggleStyles();
    });

    btnMic_ = addButton("mic", "\uE73B", sc::core::I18n::get()->get("video.recordMic"));
    btnMic_->onClick.clear();
    btnMic_->onClick.add([this](Button*) {
        if (formatIndex_ != 0) return;
        useMic_ = !useMic_;
        updateToggleStyles();
    });

    addSeparator();

    addButton("start", "\uE660", sc::core::I18n::get()->get("video.startRecord"));
    addButton("close", "\uE62D", sc::core::I18n::get()->get("video.exit"));

    updateToggleStyles();
    resize(static_cast<uint32_t>(std::lround(214.f * dpi)),
           static_cast<uint32_t>(std::lround(32.f * dpi)));
}

void ToolVideo::updateToggleStyles() {
    auto apply = [](Button* b, bool active) {
        if (!b) return;
        if (active) {
            b->setBg(0xE6F4FFFF);
            b->hoverBgColor = 0xE6F4FFFF;
            b->setColor(0x1677FFFF);
        } else {
            b->setBg(0x00000000);
            b->hoverBgColor = 0xF2F2F2FF;
            b->setColor(0x333333FF);
        }
    };
    apply(btnMp4_, formatIndex_ == 0);
    apply(btnGif_, formatIndex_ == 1);
    apply(btnSpeaker_, formatIndex_ == 0 && useSpeaker_);
    apply(btnMic_, formatIndex_ == 0 && useMic_);
    refresh();
}

void ToolVideo::showRecording(bool recording) {
    if (!recording) {
        rebuildSettingBar();
        return;
    }
    recording_ = true;
    btnMp4_ = nullptr;
    btnGif_ = nullptr;
    btnSpeaker_ = nullptr;
    btnMic_ = nullptr;

    body->children.clear();
    body->setBg(0xFFFFFFFF);
    body->setBorder(1.f, 0xA8A8A8FF);
    body->setFlexDirection(FlexDirection::Row);
    body->setAlignItems(Align::Center);

    timerLabel_ = body->makeChild<Label>();
    timerLabel_->setSize(112.f, 30.f);
    timerLabel_->setText(formatIndex_ == 1 ? "00:00 / 06:00" : "00:00 / 120:00");
    timerLabel_->setJustifyContent(Justify::Center);
    timerLabel_->setAlignItems(Align::Center);
    addSeparator();
    addButton("discard", "\uE62D", sc::core::I18n::get()->get("video.stopExit"));
    addButton("save", "\uE608", sc::core::I18n::get()->get("video.stopFile"));
    addButton("clipboard", "\uE6AD", sc::core::I18n::get()->get("video.stopClipboard"));
    resize(static_cast<uint32_t>(std::lround(209.f * dpi)),
           static_cast<uint32_t>(std::lround(32.f * dpi)));
}

void ToolVideo::setTimerText(const std::string& text) {
    if (timerLabel_) {
        timerLabel_->setText(text);
        refresh();
    }
}

} // namespace sc::windows
