#include "screencapture/gfx/text_layout.h"
#include <fontconfig/fontconfig.h>

namespace sc::gfx {

FontRegistry* FontRegistry::get() {
    static FontRegistry registry;
    return &registry;
}

void FontRegistry::addFontFile(const std::string& path) {
    if (!FcInit()) return;
    FcConfig* cfg = FcConfigGetCurrent();
    if (!cfg) return;
    if (!FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(path.c_str()))) {
        g_warning("FontRegistry: failed to add font file %s", path.c_str());
        return;
    }
    // The default Pango fontmap caches its font list at creation time; rebuild
    // it so the newly added fontconfig application font becomes visible.
    resetFontMap();
}
void FontRegistry::resetFontMap() {
    static PangoFontMap* customMap = nullptr;
    if (!customMap) {
        customMap = pango_cairo_font_map_new();
        pango_cairo_font_map_set_default(PANGO_CAIRO_FONT_MAP(customMap));
    }
}

TextLayout::TextLayout(std::string_view text, float fontSizePx, float maxW, float maxH,
                       std::string_view family)
    : text_(text), fontSize_(fontSizePx), maxW_(maxW), maxH_(maxH), family_(family) {
    PangoFontMap* fontMap = pango_cairo_font_map_get_default();
    context_.reset(pango_font_map_create_context(fontMap));
    layout_.reset(pango_layout_new(context_.get()));
    rebuild();
}

void TextLayout::setText(std::string_view text) { text_ = text; rebuild(); }
void TextLayout::setMaxWidth(float w) { maxW_ = w; rebuild(); }
void TextLayout::setFontSize(float size) { fontSize_ = size; rebuild(); }
void TextLayout::setFamily(std::string_view family) { family_ = family; rebuild(); }
void TextLayout::setBold(bool bold) { bold_ = bold; rebuild(); }
void TextLayout::setItalic(bool italic) { italic_ = italic; rebuild(); }
void TextLayout::setAlignment(PangoAlignment align) { align_ = align; rebuild(); }
void TextLayout::setWrap(bool wrap) { wrap_ = wrap; rebuild(); }

void TextLayout::rebuild() {
    if (!layout_) return;
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, family_.c_str());
    pango_font_description_set_absolute_size(desc, static_cast<int>(fontSize_ * PANGO_SCALE));
    pango_font_description_set_weight(desc, bold_ ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(desc, italic_ ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_layout_set_font_description(layout_.get(), desc);
    pango_font_description_free(desc);

    pango_layout_set_text(layout_.get(), text_.c_str(), static_cast<int>(text_.size()));
    if (maxW_ > 0.f && maxW_ < 9999.f) {
        pango_layout_set_width(layout_.get(), static_cast<int>(maxW_ * PANGO_SCALE));
        pango_layout_set_wrap(layout_.get(), wrap_ ? PANGO_WRAP_WORD_CHAR : PANGO_WRAP_WORD);
    } else {
        pango_layout_set_width(layout_.get(), -1);
    }
    pango_layout_set_alignment(layout_.get(), align_);
}

core::SizeF TextLayout::metrics() const {
    if (!layout_) return {};
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout_.get(), &w, &h);
    return {static_cast<float>(w), static_cast<float>(h)};
}
int TextLayout::lineCount() const {
    return layout_ ? pango_layout_get_line_count(layout_.get()) : 0;
}

} // namespace sc::gfx
