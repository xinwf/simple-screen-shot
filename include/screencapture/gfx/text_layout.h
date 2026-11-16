#pragma once
#include "screencapture/core/types.h"
#include <pango/pangocairo.h>
#include <memory>
#include <string>

namespace sc::gfx {

using PangoLayoutPtr = std::unique_ptr<PangoLayout, decltype(&g_object_unref)>;
using PangoContextPtr = std::unique_ptr<PangoContext, decltype(&g_object_unref)>;

// Registers iconfont.ttf via Fontconfig and provides Pango text measurement.
class FontRegistry {
public:
    static FontRegistry* get();
    void addFontFile(const std::string& path);
    void resetFontMap();
private:
    FontRegistry() = default;
};

// RAII wrapper over PangoLayout with UTF-8 text (no std::wstring anywhere).
class TextLayout {
public:
    TextLayout(std::string_view text, float fontSizePx, float maxW = 10000.f, float maxH = 10000.f,
               std::string_view family = "Sans");
    ~TextLayout() = default;
    TextLayout(TextLayout&&) noexcept = default;
    TextLayout& operator=(TextLayout&&) noexcept = default;
    TextLayout(const TextLayout&) = delete;
    TextLayout& operator=(const TextLayout&) = delete;

    void setText(std::string_view text);
    void setMaxWidth(float w);
    void setMaxHeight(float h) { maxH_ = h; }
    void setFontSize(float size);
    void setFamily(std::string_view family);
    void setBold(bool bold);
    void setItalic(bool italic);
    void setAlignment(PangoAlignment align);
    void setWrap(bool wrap);

    core::SizeF metrics() const;          // pixel size
    int lineCount() const;
    PangoLayout* native() const noexcept { return layout_.get(); }

private:
    void rebuild();
    std::string text_;
    float fontSize_{14.f};
    float maxW_{10000.f};
    float maxH_{10000.f};
    std::string family_{"Sans"};
    bool bold_{false}, italic_{false};
    PangoAlignment align_{PANGO_ALIGN_LEFT};
    bool wrap_{false};
    PangoContextPtr context_{nullptr, &g_object_unref};
    PangoLayoutPtr layout_{nullptr, &g_object_unref};
};

} // namespace sc::gfx
