#pragma once
#include "screencapture/shapes/shape_base.h"
#include <memory>
#include <string>
#include <vector>

namespace sc::shapes {

class ShapeHistory {
public:
    explicit ShapeHistory(ShapeHost* host) : host(host) {}

    ShapeBase* createShape(const std::string& toolId, float x, float y);
    void undo();
    void redo();
    void removeHoverShape(ShapeBase* hover);
    void removeShape(ShapeBase* target);
    void clear();

    std::vector<std::unique_ptr<ShapeBase>> shapes;

private:
    void removeUndoneTail();
    ShapeHost* host;
};

} // namespace sc::shapes
