#pragma once
#include "screencapture/core/types.h"
#include "screencapture/gfx/canvas_context.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace sc::ui {

class BaseWindow;

enum class FlexDirection { Row, Column };
enum class Align { FlexStart, Center, FlexEnd, Stretch };
enum class Justify { FlexStart, Center, FlexEnd, SpaceBetween };
enum class Position { Relative, Absolute };
enum class Edge { Left, Top, Right, Bottom };

// Lightweight self-drawn node tree (Node / Label / Button / Slider / TextBox / Canvas).
class WidgetNode {
public:
    explicit WidgetNode(BaseWindow* owner = nullptr) : window(owner) {}
    virtual ~WidgetNode() = default;

    template <typename T, typename... Args>
    T* makeChild(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        child->window = this->window;
        child->parent = this;
        T* raw = child.get();
        children.push_back(std::move(child));
        return raw;
    }

    void removeAllChildren() { children.clear(); }
    void setSize(float w, float h) { width = w; height = h; origWidth = w; origHeight = h; }
    void setWidth(float w) { width = w; origWidth = w; }
    void setHeight(float h) { height = h; origHeight = h; }
    void setPosition(float px, float py) {
        this->x = px;
        this->y = py;
        this->positionType = Position::Absolute;
        edges[0] = px;
        edges[1] = py;
    }
    void setBg(uint32_t rgba) { bgColor = rgba; hasBg = (rgba != 0); }
    void setBorder(float w, uint32_t rgba) { borderWidth = w; borderColor = rgba; }
    void setBorderRadius(float r) { borderRadius = r; }
    void setMargin(float l, float t, float r, float b) {
        margin[0] = l; margin[1] = t; margin[2] = r; margin[3] = b;
    }
    void setFlexDirection(FlexDirection dir) { flexDir = dir; }
    void setAlignItems(Align a) { alignItems = a; }
    void setJustifyContent(Justify j) { justifyContent = j; }
    void setPositionType(Position p) { positionType = p; }
    void setPosition(Edge edge, float value) {
        positionType = Position::Absolute;
        edges[static_cast<int>(edge)] = value;
        hasEdge[static_cast<int>(edge)] = true;
    }
    void setFlexGrow(float v) { flexGrow = v; }

    bool containsPoint(float px, float py) const noexcept {
        return visible && px >= x && px <= x + width && py >= y && py <= y + height;
    }

    virtual void computeLayout(float parentX, float parentY, float parentW, float parentH, float dpi);
    virtual void paint(gfx::CanvasContext& ctx, float dpi);
    virtual WidgetNode* hitTest(float px, float py);

    BaseWindow* window{nullptr};
    WidgetNode* parent{nullptr};
    std::vector<std::unique_ptr<WidgetNode>> children;
    std::string id;
    float x{0.f}, y{0.f}, width{0.f}, height{0.f};
    float origWidth{0.f}, origHeight{0.f};
    float edges[4]{0.f, 0.f, 0.f, 0.f};
    bool hasEdge[4]{false, false, false, false};
    float margin[4]{0.f, 0.f, 0.f, 0.f}; // L, T, R, B in logical px
    float borderRadius{0.f};
    float dpiScale{1.f};
    bool visible{true};
    bool hasBg{false};
    uint32_t bgColor{0xFFFFFFFF};
    float borderWidth{0.f};
    uint32_t borderColor{0x00000000};
    float flexGrow{0.f};
    FlexDirection flexDir{FlexDirection::Row};
    Align alignItems{Align::FlexStart};
    Justify justifyContent{Justify::FlexStart};
    Position positionType{Position::Relative};
    std::string tooltip;
};

class Label : public WidgetNode {
public:
    using WidgetNode::WidgetNode;
    void setText(std::string t) { text = std::move(t); }
    void setFontSize(float s) { fontSize = s; }
    void setColor(uint32_t rgba) { textColor = rgba; }
    void setFontFamily(std::string family) { fontFamily = std::move(family); }
    void paint(gfx::CanvasContext& ctx, float dpi) override;
    std::string text;
    float fontSize{13.f};
    uint32_t textColor{0x222222FF};
    std::string fontFamily{"Sans"}; // "icon" renders the bundled iconfont glyphs
};

class Button : public Label {
public:
    using Label::Label;
    void paint(gfx::CanvasContext& ctx, float dpi) override;
    core::Event<Button*> onClick;
    core::Event<Button*> onEnter;
    core::Event<Button*> onLeave;
    uint32_t hoverBgColor{0xF2F2F2FF};
    uint32_t hoverTextColor{0};
    bool hovered{false};
};

class Slider : public WidgetNode {
public:
    using WidgetNode::WidgetNode;
    void setRange(float minV, float maxV) { minVal = minV; maxVal = maxV; }
    void setValue(float v) { value = std::clamp(v, minVal, maxVal); }
    float getValue() const { return value; }
    float valueAt(float px) const;
    void paint(gfx::CanvasContext& ctx, float dpi) override;
    core::Event<Slider*, float> onValueChanged;
    float minVal{1.f}, maxVal{20.f}, value{4.f};
};

class TextBox : public WidgetNode {
public:
    using WidgetNode::WidgetNode;
    void setText(std::string t) { text = std::move(t); caretByte_ = text.size(); }
    const std::string& getText() const { return text; }
    void setColor(uint32_t c) { textColor = c; }
    void setCaretColor(uint32_t c) { caretColor = c; }
    void setFontSize(float s) { fontSize = s; }
    void focus() { isFocused = true; onFocusChanged.emit(this, true); }
    void blur() { isFocused = false; onFocusChanged.emit(this, false); }

    // Caret-aware editing with selection. The caret and selection anchor are
    // UTF-8 byte offsets inside `text`.
    void insertAtCaret(const std::string& s);
    bool backspace();          // remove selection, or the char left of the caret
    bool deleteChar();         // remove selection, or the char at the caret
    bool caretMove(int dir, bool extend = false);  // -1/+1 char; extend keeps the anchor
    void caretToStart(bool extend = false);
    void caretToEnd(bool extend = false);
    void setCaretByte(size_t b);                    // clears the selection
    void setCaretExtend(size_t b);                  // move caret, keep anchor (shift+click)
    void selectWordAt(size_t bytePos);              // double-click word selection
    bool hasSelection() const { return selAnchor_ != caretByte_; }
    std::pair<size_t, size_t> selectionRange() const;
    // Map a window-space x to a caret byte offset (nearest char boundary).
    size_t caretIndexFromX(float px, float dpi) const;

    void paint(gfx::CanvasContext& ctx, float dpi) override;
    core::Event<TextBox*, const std::string&> onTextChanged;
    core::Event<TextBox*, bool> onFocusChanged;
    std::string text;
    uint32_t textColor{0xFF0000FF};
    uint32_t caretColor{0xFF0000FF};
    float fontSize{16.f};
    bool isFocused{false};

private:
    size_t caretByte_{0};
    size_t selAnchor_{0};
    static size_t nextBoundary(std::string_view s, size_t i);
    static size_t prevBoundary(std::string_view s, size_t i);
    float prefixWidth(std::string_view prefix, float dpi) const;
    bool deleteSelection();
};

class CanvasNode : public WidgetNode {
public:
    using WidgetNode::WidgetNode;
    void paint(gfx::CanvasContext& ctx, float dpi) override {
        onPaint.emit(ctx);
        WidgetNode::paint(ctx, dpi);
    }
    core::Event<gfx::CanvasContext&> onPaint;
};

} // namespace sc::ui
