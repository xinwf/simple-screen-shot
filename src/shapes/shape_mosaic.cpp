#include "screencapture/shapes/shape_mosaic.h"
#include "screencapture/shapes/shape_history.h"
#include "screencapture/shapes/stroke_geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sc::shapes {

ShapeMosaic::ShapeMosaic(ShapeHost* host, const std::string& toolId)
    : ShapeBase(host), toolId(toolId) {
    applyStyle();
}

void ShapeMosaic::applyStyle() {
    strokeWidth = host->sliderValue(toolId);
    isRect = host->toolFlag(toolId, "rect");
    resetMosaic();
    buildMosaicBitmap();
}

void ShapeMosaic::resetMosaic() {
    mosaicSurface.reset();
    mosaicOrigin = {0.f, 0.f};
}

void ShapeMosaic::makePath() {
    detail::buildStrokePath(path, linePoints);
    pathValid = !linePoints.empty();
}

void ShapeMosaic::updateDraggers() {
    draggers.clear();
    float half = draggerSize / 2.f;
    auto d = [half](float x, float y) { return core::RectF{x-half,y-half,x+half,y+half}; };
    if (isRect) {
        const float l=rect.left,t=rect.top,r=rect.right,b=rect.bottom;
        const float mx=(l+r)*0.5f, my=(t+b)*0.5f;
        draggers = {d(l,t),d(mx,t),d(r,t),d(r,my),d(r,b),d(mx,b),d(l,b),d(l,my)};
    } else if (!linePoints.empty()) {
        draggers = {d(linePoints.front().x,linePoints.front().y),
                    d(linePoints.back().x,linePoints.back().y)};
    }
}

void ShapeMosaic::mosaicPixels(uint8_t* bits, uint32_t pitch, uint32_t width, uint32_t height, int blockSize) {
    if (!bits || blockSize <= 0 || width == 0 || height == 0) return;
    for (uint32_t y = 0; y < height; y += static_cast<uint32_t>(blockSize)) {
        uint32_t yEnd = std::min(y + static_cast<uint32_t>(blockSize), height);
        for (uint32_t x = 0; x < width; x += static_cast<uint32_t>(blockSize)) {
            uint32_t xEnd = std::min(x + static_cast<uint32_t>(blockSize), width);
            unsigned long long bs=0, gs=0, rs=0, as=0, count=0;
            for (uint32_t yy = y; yy < yEnd; ++yy) {
                auto* row = bits + static_cast<size_t>(yy) * pitch;
                for (uint32_t xx = x; xx < xEnd; ++xx) {
                    auto* p = row + static_cast<size_t>(xx) * 4;
                    bs += p[0]; gs += p[1]; rs += p[2]; as += p[3];
                    ++count;
                }
            }
            if (!count) continue;
            uint8_t nb = static_cast<uint8_t>(bs/count), ng = static_cast<uint8_t>(gs/count);
            uint8_t nr = static_cast<uint8_t>(rs/count), na = static_cast<uint8_t>(as/count);
            for (uint32_t yy = y; yy < yEnd; ++yy) {
                auto* row = bits + static_cast<size_t>(yy) * pitch;
                for (uint32_t xx = x; xx < xEnd; ++xx) {
                    auto* p = row + static_cast<size_t>(xx) * 4;
                    p[0]=nb; p[1]=ng; p[2]=nr; p[3]=na;
                }
            }
        }
    }
}

void ShapeMosaic::buildMosaicBitmap() {
    auto screen = host->screenSurface();
    if (!screen || screen->width() == 0 || screen->height() == 0) return;

    int blockSize = std::max(6, static_cast<int>(std::round(strokeWidth / 3.f)));
    int sw = static_cast<int>(screen->width()), sh = static_cast<int>(screen->height());
    mosaicOrigin = {0.f, 0.f};

    auto tmp = std::make_shared<gfx::ImageSurface>(sw, sh);
    auto ctx = gfx::CanvasContext::fromSurface(*tmp);
    ctx->drawImage(*screen, {0.f, 0.f, static_cast<float>(sw), static_cast<float>(sh)});
    if (auto* hist = host->history()) {
        for (const auto& s : hist->shapes) {
            if (s.get() == this) break;
            if (!s->isUndo) s->paint(*ctx);
        }
    }
    ctx.reset();
    tmp->flush();
    mosaicPixels(tmp->data(), tmp->stride(), tmp->width(), tmp->height(), blockSize);
    tmp->markDirty();
    mosaicSurface = tmp;
}

void ShapeMosaic::paint(gfx::CanvasContext& ctx) {
    if (!mosaicSurface) buildMosaicBitmap();
    if (isRect) {
        if (!rect.isEmpty()) {
            if (mosaicSurface && mosaicSurface->native()) {
                cairo_t* cr = ctx.native();
                cairo_save(cr);
                cairo_set_source_surface(cr, mosaicSurface->native(), mosaicOrigin.x, mosaicOrigin.y);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
                cairo_rectangle(cr, rect.left, rect.top, rect.width(), rect.height());
                cairo_fill(cr);
                cairo_restore(cr);
            } else {
                ctx.fillRect(rect, kPlaceholder);
            }
        }
        return;
    }
    if (!pathValid) return;
    if (mosaicSurface && mosaicSurface->native()) {
        cairo_t* cr = ctx.native();
        cairo_save(cr);
        cairo_set_source_surface(cr, mosaicSurface->native(), mosaicOrigin.x, mosaicOrigin.y);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_width(cr, strokeWidth);
        path.replay(cr);
        cairo_stroke(cr);
        cairo_restore(cr);
    } else {
        ctx.setLineCap(CAIRO_LINE_CAP_ROUND);
        ctx.setLineJoin(CAIRO_LINE_JOIN_ROUND);
        ctx.strokePath(path, kPlaceholder, strokeWidth);
    }
}

void ShapeMosaic::paintDragger(gfx::CanvasContext& ctx) {
    size_t count = isRect ? draggers.size() : std::min<size_t>(2, draggers.size());
    for (size_t i = 0; i < count; ++i) {
        ctx.fillRect(draggers[i], core::ColorRgba::fromHex(0xFFFFFF));
        ctx.strokeRect(draggers[i], draggerColor, host->hostDpi());
    }
}

void ShapeMosaic::mouseDown(float x, float y) {
    if (!mosaicSurface) buildMosaicBitmap();
    if (isRect) {
        if (hoverDraggerIndex == -1) {
            pressX = x; pressY = y; rect = {x,y,x,y}; hoverDraggerIndex = 4;
        } else if (hoverDraggerIndex == 0) { pressX=rect.right; pressY=rect.bottom; }
        else if (hoverDraggerIndex == 1) { pressY=rect.bottom; }
        else if (hoverDraggerIndex == 2) { pressX=rect.left; pressY=rect.bottom; }
        else if (hoverDraggerIndex == 3) { pressX=rect.left; }
        else if (hoverDraggerIndex == 4) { pressX=rect.left; pressY=rect.top; }
        else if (hoverDraggerIndex == 5) { pressY=rect.top; }
        else if (hoverDraggerIndex == 6) { pressX=rect.right; pressY=rect.top; }
        else if (hoverDraggerIndex == 7) { pressX=rect.right; }
        else if (hoverDraggerIndex == 8) { pressX=x-rect.left; pressY=y-rect.top; }
        return;
    }
    if (hoverDraggerIndex == -1) {
        linePoints.push_back({x,y});
        if (host->shiftDown()) linePoints.push_back({x,y});
        makePath();
        hoverDraggerIndex = 1;
    } else if (hoverDraggerIndex == 8) { pressX=x; pressY=y; }
    host->refresh();
}

void ShapeMosaic::mouseDrag(float x, float y) {
    int i = hoverDraggerIndex;
    if (i == -1) return;
    if (isRect) {
        if (i==0||i==2||i==4||i==6) {
            rect.left=std::min(pressX,x); rect.right=std::max(pressX,x);
            rect.top=std::min(pressY,y); rect.bottom=std::max(pressY,y);
        } else if (i==1||i==5) { rect.top=std::min(pressY,y); rect.bottom=std::max(pressY,y); }
        else if (i==3||i==7) { rect.left=std::min(pressX,x); rect.right=std::max(pressX,x); }
        else if (i==8) {
            float w=rect.width(),h=rect.height();
            rect.left=x-pressX; rect.top=y-pressY; rect.right=rect.left+w; rect.bottom=rect.top+h;
        }
        if (i != 8 && host->shiftDown()) {
            float w=rect.width(),h=rect.height();
            if (w>h) rect.bottom=rect.top+w; else rect.right=rect.left+h;
        }
        host->refresh();
        return;
    }
    if (i==0) {
        if (host->shiftDown() && !linePoints.empty()) linePoints[0]={x,y};
        else linePoints.insert(linePoints.begin(),{x,y});
        makePath();
    } else if (i==1) {
        if (host->shiftDown() && !linePoints.empty()) linePoints.back()={x,y};
        else linePoints.push_back({x,y});
        makePath();
    } else if (i==8) {
        float sx=x-pressX, sy=y-pressY;
        for (auto& p:linePoints){p.x+=sx;p.y+=sy;}
        makePath(); pressX=x; pressY=y;
    }
    host->refresh();
}

void ShapeMosaic::mouseUp(float, float) {
    updateDraggers();
    host->refresh();
}

void ShapeMosaic::mouseMove(float x, float y) {
    hoverDraggerIndex = -1;
    updateDraggers();
    if (isRect) {
        for (int i = 0; i < static_cast<int>(draggers.size()); ++i) {
            if (isInRect(draggers[i], x, y)) { hoverDraggerIndex = i; return; }
        }
        if (!rect.isEmpty()) {
            float half = 3.f * host->hostDpi();
            if (x>=rect.left-half&&x<=rect.right+half&&y>=rect.top-half&&y<=rect.bottom+half) {
                hoverDraggerIndex=8;
            }
        }
        return;
    }
    for (int i=0;i<static_cast<int>(draggers.size());++i)
        if (isInRect(draggers[i],x,y)) { hoverDraggerIndex=i; return; }
    hoverDraggerIndex = detail::strokeHitTest(linePoints,{x,y},strokeWidth,host->hostDpi(),draggerSize);
}

void ShapeMosaic::mouseWheel(float, float, int delta) {
    float step = 2.f * host->hostDpi();
    float next = strokeWidth + (delta < 0 ? -step : step);
    float applied = host->clampSliderValue(toolId, next);
    if (applied == strokeWidth) return;
    strokeWidth = applied;
    resetMosaic();
    buildMosaicBitmap();
    host->refresh();
}

void ShapeMosaic::setCursor() {
    if (isRect) {
        int i=hoverDraggerIndex;
        if (i==0||i==4) host->setCursorHint(4);
        else if (i==2||i==6) host->setCursorHint(5);
        else if (i==1||i==5) host->setCursorHint(6);
        else if (i==3||i==7) host->setCursorHint(7);
        else if (i==8) host->setCursorHint(1);
    } else if (hoverDraggerIndex>=0) host->setCursorHint(1);
}

} // namespace sc::shapes
