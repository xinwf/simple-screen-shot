#include "screencapture/shapes/shape_history.h"
#include "screencapture/shapes/shape_rect.h"
#include "screencapture/shapes/shape_ellipse.h"
#include "screencapture/shapes/shape_line.h"
#include "screencapture/shapes/shape_arrow.h"
#include "screencapture/shapes/shape_number.h"
#include "screencapture/shapes/shape_text.h"
#include "screencapture/shapes/shape_mosaic.h"
#include "screencapture/shapes/shape_eraser.h"
#include <algorithm>

namespace sc::shapes {

void ShapeHistory::removeUndoneTail() {
    int i = static_cast<int>(shapes.size()) - 1;
    while (i >= 0 && shapes[i]->isUndo) {
        shapes.erase(shapes.begin() + i);
        --i;
    }
}

ShapeBase* ShapeHistory::createShape(const std::string& toolId, float x, float y) {
    removeUndoneTail();
    while (shapes.size() >= kMaxHistory) {
        shapes.erase(shapes.begin());
    }
    std::unique_ptr<ShapeBase> shape;
    if (toolId == "rect") shape = std::make_unique<ShapeRect>(host, toolId);
    else if (toolId == "ellipse") shape = std::make_unique<ShapeEllipse>(host, toolId);
    else if (toolId == "arrow") shape = std::make_unique<ShapeArrow>(host, toolId);
    else if (toolId == "number") shape = std::make_unique<ShapeNumber>(host, toolId);
    else if (toolId == "line") shape = std::make_unique<ShapeLine>(host, toolId);
    else if (toolId == "text") shape = std::make_unique<ShapeText>(host, toolId);
    else if (toolId == "mosaic") shape = std::make_unique<ShapeMosaic>(host, toolId);
    else if (toolId == "eraser") shape = std::make_unique<ShapeEraser>(host, toolId);
    else return nullptr;

    auto* raw = shape.get();
    shapes.push_back(std::move(shape));
    raw->mouseDown(x, y);
    return raw;
}

void ShapeHistory::undo() {
    for (int i = static_cast<int>(shapes.size()) - 1; i >= 0; --i) {
        if (!shapes[i]->isUndo) {
            shapes[i]->isUndo = true;
            host->refresh();
            break;
        }
    }
}

void ShapeHistory::redo() {
    for (auto& shape : shapes) {
        if (shape->isUndo) {
            shape->isUndo = false;
            host->refresh();
            break;
        }
    }
}

void ShapeHistory::removeHoverShape(ShapeBase* hover) {
    if (!hover) return;
    if (auto* text = dynamic_cast<ShapeText*>(hover)) text->finishEdit();
    removeShape(hover);
}

void ShapeHistory::removeShape(ShapeBase* target) {
    auto it = std::find_if(shapes.begin(), shapes.end(),
                           [target](const auto& s) { return s.get() == target; });
    if (it != shapes.end()) {
        shapes.erase(it);
        host->refresh();
    }
}

void ShapeHistory::clear() { shapes.clear(); }

} // namespace sc::shapes
