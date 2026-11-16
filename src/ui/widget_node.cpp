#include "screencapture/ui/widget_node.h"
#include "screencapture/gfx/text_layout.h"
#include <cmath>
#include <cstdio>

namespace sc::ui {

void WidgetNode::computeLayout(float parentX, float parentY, float parentW, float parentH, float dpi) {
    if (!visible) return;
    dpiScale = dpi;
    if (positionType == Position::Absolute) {
        x = parentX + edges[static_cast<int>(Edge::Left)] * dpi;
        y = parentY + edges[static_cast<int>(Edge::Top)] * dpi;
        if (origWidth > 0.f) {
            width = origWidth * dpi;
        } else if (hasEdge[static_cast<int>(Edge::Right)]) {
            width = std::max(0.f, parentW - (edges[0] + edges[2]) * dpi);
        } else {
            width = std::max(0.f, parentW - edges[0] * dpi);
        }
        if (origHeight > 0.f) {
            height = origHeight * dpi;
        } else if (hasEdge[static_cast<int>(Edge::Bottom)]) {
            height = std::max(0.f, parentH - (edges[1] + edges[3]) * dpi);
        } else {
            height = std::max(0.f, parentH - edges[1] * dpi);
        }
    }

    // First pass: resolve fixed sizes and sum flexGrow for relative children.
    float fixedMain = 0.f;
    float totalGrow = 0.f;
    for (auto& child : children) {
        if (!child->visible || child->positionType != Position::Relative) continue;
        float ml = child->margin[0] * dpi, mt = child->margin[1] * dpi;
        float mr = child->margin[2] * dpi, mb = child->margin[3] * dpi;

        if (child->origWidth > 0.f) child->width = child->origWidth * dpi;
        if (child->origHeight > 0.f) child->height = child->origHeight * dpi;

        if (flexDir == FlexDirection::Row) {
            if (child->origHeight <= 0.f) {
                child->height = std::max(0.f, height - mt - mb);
            }
            if (child->flexGrow > 0.f && child->origWidth <= 0.f) {
                totalGrow += child->flexGrow;
                fixedMain += ml + mr;
            } else {
                fixedMain += child->width + ml + mr;
            }
        } else {
            if (child->origWidth <= 0.f) {
                child->width = std::max(0.f, width - ml - mr);
            }
            if (child->flexGrow > 0.f && child->origHeight <= 0.f) {
                totalGrow += child->flexGrow;
                fixedMain += mt + mb;
            } else {
                fixedMain += child->height + mt + mb;
            }
        }
    }

    float mainSpan = (flexDir == FlexDirection::Row) ? width : height;
    float extraMain = std::max(0.f, mainSpan - fixedMain);

    float startOffset = 0.f;
    if (totalGrow <= 0.f && justifyContent == Justify::Center) {
        startOffset = (mainSpan - fixedMain) * 0.5f;
    } else if (totalGrow <= 0.f && justifyContent == Justify::FlexEnd) {
        startOffset = mainSpan - fixedMain;
    }

    float curX = x + (flexDir == FlexDirection::Row ? startOffset : 0.f);
    float curY = y + (flexDir == FlexDirection::Column ? startOffset : 0.f);

    for (auto& child : children) {
        if (!child->visible) continue;
        if (child->positionType == Position::Relative) {
            float ml = child->margin[0] * dpi, mt = child->margin[1] * dpi;
            float mr = child->margin[2] * dpi, mb = child->margin[3] * dpi;

            if (flexDir == FlexDirection::Row) {
                if (child->flexGrow > 0.f && child->origWidth <= 0.f && totalGrow > 0.f) {
                    child->width = extraMain * (child->flexGrow / totalGrow);
                }
                curX += ml;
                child->x = curX;
                if (alignItems == Align::Center) {
                    child->y = y + (height - child->height) * 0.5f;
                } else if (alignItems == Align::FlexEnd) {
                    child->y = y + height - child->height - mb;
                } else {
                    child->y = y + mt;
                }
                curX += child->width + mr;
            } else {
                if (child->flexGrow > 0.f && child->origHeight <= 0.f && totalGrow > 0.f) {
                    child->height = extraMain * (child->flexGrow / totalGrow);
                }
                curY += mt;
                child->y = curY;
                if (alignItems == Align::Center) {
                    child->x = x + (width - child->width) * 0.5f;
                } else if (alignItems == Align::FlexEnd) {
                    child->x = x + width - child->width - mr;
                } else {
                    child->x = x + ml;
                }
                curY += child->height + mb;
            }
        }
        // Pass THIS node's (the parent's) geometry so Absolute children compute
        // their bounds relative to their parent instead of accumulating on their own x/y!
        child->computeLayout(this->x, this->y, this->width, this->height, dpi);
    }
}

void WidgetNode::paint(gfx::CanvasContext& ctx, float dpi) {
    if (!visible) return;
    core::RectF r{x, y, x + width, y + height};
    if (hasBg && (bgColor & 0xFF) != 0) {
        if (borderRadius > 0.f) {
            ctx.fillRoundedRect(r, borderRadius * dpi, core::ColorRgba::fromRgbaHex(bgColor));
        } else {
            ctx.fillRect(r, core::ColorRgba::fromRgbaHex(bgColor));
        }
    }
    if (borderWidth > 0.f && (borderColor & 0xFF) != 0) {
        float bw = borderWidth * dpi;
        core::RectF br{x + bw * 0.5f, y + bw * 0.5f, x + width - bw * 0.5f, y + height - bw * 0.5f};
        if (borderRadius > 0.f) {
            ctx.strokeRoundedRect(br, borderRadius * dpi, core::ColorRgba::fromRgbaHex(borderColor), bw);
        } else {
            ctx.strokeRect(br, core::ColorRgba::fromHex(borderColor >> 8), bw);
        }
    }
    for (auto& child : children) child->paint(ctx, dpi);
}

WidgetNode* WidgetNode::hitTest(float px, float py) {
    if (!visible || px < x || px > x + width || py < y || py > y + height) return nullptr;
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (auto* hit = (*it)->hitTest(px, py)) return hit;
    }
    return this;
}

void Label::paint(gfx::CanvasContext& ctx, float dpi) {
    WidgetNode::paint(ctx, dpi);
    if (text.empty()) return;
    gfx::TextLayout layout(text, fontSize * dpi, 10000.f, height, fontFamily);
    auto tm = layout.metrics();
    float tx = x + (justifyContent == Justify::Center ? (width - tm.width) * 0.5f : 4.f * dpi);
    float ty = y + (alignItems == Align::Center ? (height - tm.height) * 0.5f : 2.f * dpi);
    ctx.drawText(tx, ty, layout, core::ColorRgba::fromRgbaHex(textColor));
}

void Button::paint(gfx::CanvasContext& ctx, float dpi) {
    uint32_t savedBg = bgColor;
    bool savedHasBg = hasBg;
    uint32_t savedText = textColor;
    if (hovered) {
        if (hoverBgColor != 0) {
            bgColor = hoverBgColor;
            hasBg = true;
        }
        if (hoverTextColor != 0) {
            textColor = hoverTextColor;
        }
    }
    Label::paint(ctx, dpi);
    bgColor = savedBg;
    hasBg = savedHasBg;
    textColor = savedText;
}

float Slider::valueAt(float px) const {
    const float pad = 6.f * dpiScale;
    const float lo = x + pad;
    const float usable = std::max(1.f, width - pad * 2.f);
    float ratio = std::clamp((px - lo) / usable, 0.f, 1.f);
    return std::round(minVal + ratio * (maxVal - minVal));
}

void Slider::paint(gfx::CanvasContext& ctx, float dpi) {
    WidgetNode::paint(ctx, dpi);
    const float pad = 6.f * dpi;
    float cy = y + height * 0.5f;
    float ratio = (value - minVal) / std::max(1.f, maxVal - minVal);
    float knobX = x + pad + ratio * std::max(1.f, width - pad * 2.f);
    // Background track
    ctx.fillRoundedRect({x + pad, cy - 2.f * dpi, x + width - pad, cy + 2.f * dpi},
                        2.f * dpi, core::ColorRgba::fromHex(0xD9D9D9));
    // Active filled track
    ctx.fillRoundedRect({x + pad, cy - 2.f * dpi, knobX, cy + 2.f * dpi},
                        2.f * dpi, core::ColorRgba::fromHex(0x1677FF));
    // Thumb circle
    ctx.fillEllipse(knobX, cy, 5.5f * dpi, 5.5f * dpi, core::ColorRgba::fromHex(0x1677FF));
    ctx.fillEllipse(knobX, cy, 3.0f * dpi, 3.0f * dpi, core::ColorRgba::fromHex(0xFFFFFF));
}

namespace {

size_t tbNextBoundary(std::string_view s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}

size_t tbPrevBoundary(std::string_view s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

} // namespace

size_t TextBox::nextBoundary(std::string_view s, size_t i) { return tbNextBoundary(s, i); }
size_t TextBox::prevBoundary(std::string_view s, size_t i) { return tbPrevBoundary(s, i); }

void TextBox::insertAtCaret(const std::string& s) {
    deleteSelection();
    caretByte_ = std::min(caretByte_, text.size());
    text.insert(caretByte_, s);
    caretByte_ += s.size();
    selAnchor_ = caretByte_;
    onTextChanged.emit(this, text);
}

bool TextBox::backspace() {
    if (deleteSelection()) return true;
    if (caretByte_ == 0) return false;
    size_t prev = tbPrevBoundary(text, caretByte_);
    text.erase(prev, caretByte_ - prev);
    caretByte_ = prev;
    selAnchor_ = caretByte_;
    onTextChanged.emit(this, text);
    return true;
}

bool TextBox::deleteChar() {
    if (deleteSelection()) return true;
    if (caretByte_ >= text.size()) return false;
    size_t next = tbNextBoundary(text, caretByte_);
    text.erase(caretByte_, next - caretByte_);
    selAnchor_ = caretByte_;
    onTextChanged.emit(this, text);
    return true;
}

bool TextBox::caretMove(int dir, bool extend) {
    size_t old = caretByte_;
    if (dir < 0) {
        if (caretByte_ == 0) return false;
        caretByte_ = tbPrevBoundary(text, caretByte_);
    } else {
        if (caretByte_ >= text.size()) return false;
        caretByte_ = tbNextBoundary(text, caretByte_);
    }
    if (!extend) selAnchor_ = caretByte_;
    return caretByte_ != old || extend;
}

void TextBox::caretToStart(bool extend) {
    if (!extend) selAnchor_ = 0;
    caretByte_ = 0;
}

void TextBox::caretToEnd(bool extend) {
    if (!extend) selAnchor_ = text.size();
    caretByte_ = text.size();
}

void TextBox::setCaretByte(size_t b) {
    caretByte_ = std::min(b, text.size());
    selAnchor_ = caretByte_;
}

void TextBox::setCaretExtend(size_t b) {
    caretByte_ = std::min(b, text.size());
}

std::pair<size_t, size_t> TextBox::selectionRange() const {
    return {std::min(selAnchor_, caretByte_), std::max(selAnchor_, caretByte_)};
}

bool TextBox::deleteSelection() {
    if (!hasSelection()) return false;
    auto [b, e] = selectionRange();
    text.erase(b, e - b);
    caretByte_ = selAnchor_ = b;
    return true;
}

// Double-click word selection: a run of word characters (alnum, underscore,
// multibyte/CJK) or, clicking a separator, the separator run itself.
void TextBox::selectWordAt(size_t pos) {
    if (text.empty()) return;
    pos = std::min(pos, text.size());
    size_t probe = (pos < text.size()) ? pos : tbPrevBoundary(text, pos);
    static auto cls = [this](size_t i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0x80) return 0;                          // multibyte (CJK etc.)
        if (c == ' ' || c == '\t') return 1;             // whitespace
        if (std::isalnum(c) || c == '_') return 0;        // word
        return 2;                                          // punctuation
    };
    size_t b = probe, e = tbNextBoundary(text, probe);
    int c0 = cls(probe);
    while (b > 0 && cls(tbPrevBoundary(text, b)) == c0) b = tbPrevBoundary(text, b);
    while (e < text.size() && cls(e) == c0) e = tbNextBoundary(text, e);
    selAnchor_ = b;
    caretByte_ = e;
}

float TextBox::prefixWidth(std::string_view prefix, float dpi) const {
    gfx::TextLayout layout(prefix, fontSize * dpi, 10000.f, 10000.f);
    return layout.metrics().width;
}

size_t TextBox::caretIndexFromX(float px, float dpi) const {
    float rel = px - x - 6.f * dpi;
    size_t i = 0;
    float prevW = 0.f;
    while (i < text.size()) {
        size_t n = tbNextBoundary(text, i);
        float w = prefixWidth(text.substr(0, n), dpi);
        if (w >= rel) return (w - rel <= rel - prevW) ? n : i;
        prevW = w;
        i = n;
    }
    return text.size();
}

void TextBox::paint(gfx::CanvasContext& ctx, float dpi) {
    if (!visible) return;
    float pad = 6.f * dpi;
    gfx::TextLayout layout(text.empty() ? " " : text, fontSize * dpi, 10000.f, 10000.f);
    auto tm = layout.metrics();
    width = std::max(48.f * dpi, (text.empty() ? 12.f * dpi : tm.width) + pad * 2.f);
    height = std::max(fontSize * dpi + pad * 2.f, tm.height + pad * 2.f);
    if (hasSelection()) {
        auto [b, e] = selectionRange();
        float x1 = x + pad + prefixWidth(text.substr(0, b), dpi);
        float x2 = x + pad + prefixWidth(text.substr(0, e), dpi);
        ctx.fillRect({x1, y + pad, x2, y + height - pad},
                     core::ColorRgba::fromHex(0x3584E4, 0.45f));
    }
    if (!text.empty()) {
        ctx.drawText(x + pad, y + pad, layout, core::ColorRgba::fromRgbaHex(textColor));
    }
    if (isFocused) {
        size_t cb = std::min(caretByte_, text.size());
        float caretX = x + pad + prefixWidth(text.substr(0, cb), dpi);
        ctx.fillRect({caretX, y + pad, caretX + 1.5f * dpi, y + height - pad},
                     core::ColorRgba::fromRgbaHex(caretColor));
    }
}

} // namespace sc::ui
