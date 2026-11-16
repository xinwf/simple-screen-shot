#include "screencapture/shapes/shape_eraser.h"
#include "screencapture/shapes/stroke_geometry.h"
#include <algorithm>
#include <cmath>

namespace sc::shapes {

ShapeEraser::ShapeEraser(ShapeHost* host, const std::string& toolId)
    : ShapeBase(host), toolId(toolId) {
    applyStyle();
}

void ShapeEraser::applyStyle() {
    strokeWidth = host->sliderValue(toolId);
    isRect = host->toolFlag(toolId, "rect");
}

void ShapeEraser::resetEraser() { isErasing = true; }

void ShapeEraser::makePath() {
    detail::buildStrokePath(path, linePoints);
    pathValid = !linePoints.empty();
}

void ShapeEraser::updateDraggers() {
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

void ShapeEraser::paint(gfx::CanvasContext& ctx) {
    auto screen = host->screenSurface();
    if (isRect) {
        if (rect.isEmpty()) return;
        if (screen && screen->native()) {
            cairo_t* cr = ctx.native();
            cairo_save(cr);
            cairo_set_source_surface(cr, screen->native(), 0.0, 0.0);
            cairo_rectangle(cr, rect.left, rect.top, rect.width(), rect.height());
            cairo_fill(cr);
            cairo_restore(cr);
        } else {
            ctx.fillRect(rect, kPlaceholder);
        }
        return;
    }
    if (!pathValid) return;
    if (screen && screen->native()) {
        cairo_t* cr = ctx.native();
        cairo_save(cr);
        cairo_set_source_surface(cr, screen->native(), 0.0, 0.0);
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

void ShapeEraser::paintDragger(gfx::CanvasContext& ctx) {
    size_t count = isRect ? draggers.size() : std::min<size_t>(2, draggers.size());
    for (size_t i = 0; i < count; ++i) {
        ctx.fillRect(draggers[i], core::ColorRgba::fromHex(0xFFFFFF));
        ctx.strokeRect(draggers[i], draggerColor, host->hostDpi());
    }
}

void ShapeEraser::mouseDown(float x, float y) {
    if (isRect) {
        if (hoverDraggerIndex == -1) {
            pressX=x; pressY=y; rect={x,y,x,y}; hoverDraggerIndex=4;
        } else if (hoverDraggerIndex==0){pressX=rect.right;pressY=rect.bottom;}
        else if (hoverDraggerIndex==1){pressY=rect.bottom;}
        else if (hoverDraggerIndex==2){pressX=rect.left;pressY=rect.bottom;}
        else if (hoverDraggerIndex==3){pressX=rect.left;}
        else if (hoverDraggerIndex==4){pressX=rect.left;pressY=rect.top;}
        else if (hoverDraggerIndex==5){pressY=rect.top;}
        else if (hoverDraggerIndex==6){pressX=rect.right;pressY=rect.top;}
        else if (hoverDraggerIndex==7){pressX=rect.right;}
        else if (hoverDraggerIndex==8){pressX=x-rect.left;pressY=y-rect.top;}
        return;
    }
    if (hoverDraggerIndex==-1) {
        linePoints.push_back({x,y});
        if (host->shiftDown()) linePoints.push_back({x,y});
        makePath(); hoverDraggerIndex=1;
    } else if (hoverDraggerIndex==8){pressX=x;pressY=y;}
    host->refresh();
}

void ShapeEraser::mouseDrag(float x, float y) {
    int i=hoverDraggerIndex;
    if (i==-1) return;
    if (isRect) {
        if (i==0||i==2||i==4||i==6){
            rect.left=std::min(pressX,x);rect.right=std::max(pressX,x);
            rect.top=std::min(pressY,y);rect.bottom=std::max(pressY,y);
        } else if (i==1||i==5){rect.top=std::min(pressY,y);rect.bottom=std::max(pressY,y);}
        else if (i==3||i==7){rect.left=std::min(pressX,x);rect.right=std::max(pressX,x);}
        else if (i==8){float w=rect.width(),h=rect.height();rect.left=x-pressX;rect.top=y-pressY;rect.right=rect.left+w;rect.bottom=rect.top+h;}
        if (i!=8&&host->shiftDown()){float w=rect.width(),h=rect.height();if(w>h)rect.bottom=rect.top+w;else rect.right=rect.left+h;}
        host->refresh(); return;
    }
    if (i==0){if(host->shiftDown()&&!linePoints.empty())linePoints[0]={x,y};else linePoints.insert(linePoints.begin(),{x,y});makePath();}
    else if(i==1){if(host->shiftDown()&&!linePoints.empty())linePoints.back()={x,y};else linePoints.push_back({x,y});makePath();}
    else if(i==8){float sx=x-pressX,sy=y-pressY;for(auto&p:linePoints){p.x+=sx;p.y+=sy;}makePath();pressX=x;pressY=y;}
    host->refresh();
}

void ShapeEraser::mouseUp(float, float) {
    updateDraggers();
    host->refresh();
}

void ShapeEraser::mouseMove(float x, float y) {
    hoverDraggerIndex=-1;
    updateDraggers();
    if (isRect){
        for(int i=0;i<static_cast<int>(draggers.size());++i) if(isInRect(draggers[i],x,y)){hoverDraggerIndex=i;return;}
        if(!rect.isEmpty()){
            float half=3.f*host->hostDpi();
            if(x>=rect.left-half&&x<=rect.right+half&&y>=rect.top-half&&y<=rect.bottom+half)
                hoverDraggerIndex=8;
        }
        return;
    }
    for(int i=0;i<static_cast<int>(draggers.size());++i) if(isInRect(draggers[i],x,y)){hoverDraggerIndex=i;return;}
    hoverDraggerIndex=detail::strokeHitTest(linePoints,{x,y},strokeWidth,host->hostDpi(),draggerSize);
}

void ShapeEraser::mouseWheel(float, float, int delta) {
    float step = 2.f * host->hostDpi();
    float next = strokeWidth + (delta < 0 ? -step : step);
    float applied = host->clampSliderValue(toolId, next);
    if (applied == strokeWidth) return;
    strokeWidth = applied;
    host->refresh();
}

void ShapeEraser::setCursor() {
    if (isRect){
        int i=hoverDraggerIndex;
        if(i==0||i==4)host->setCursorHint(4);
        else if(i==2||i==6)host->setCursorHint(5);
        else if(i==1||i==5)host->setCursorHint(6);
        else if(i==3||i==7)host->setCursorHint(7);
        else if(i==8)host->setCursorHint(1);
    } else if(hoverDraggerIndex>=0)host->setCursorHint(1);
}

} // namespace sc::shapes
