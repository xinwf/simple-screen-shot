#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace sc::core {

struct PointI {
    int32_t x{0};
    int32_t y{0};
    constexpr bool operator==(const PointI& o) const noexcept { return x == o.x && y == o.y; }
};

struct PointF {
    float x{0.f};
    float y{0.f};
    constexpr bool operator==(const PointF& o) const noexcept { return x == o.x && y == o.y; }
};

struct SizeU {
    uint32_t width{0};
    uint32_t height{0};
};

struct SizeF {
    float width{0.f};
    float height{0.f};
};

struct RectI {
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};
    constexpr int32_t width() const noexcept { return right - left; }
    constexpr int32_t height() const noexcept { return bottom - top; }
    constexpr bool isEmpty() const noexcept { return right <= left || bottom <= top; }
};

struct RectF {
    float left{0.f};
    float top{0.f};
    float right{0.f};
    float bottom{0.f};
    constexpr float width() const noexcept { return right - left; }
    constexpr float height() const noexcept { return bottom - top; }
    constexpr bool isEmpty() const noexcept { return right <= left || bottom <= top; }
    constexpr bool contains(float px, float py) const noexcept {
        return px >= left && px <= right && py >= top && py <= bottom;
    }
    constexpr PointF center() const noexcept {
        return {(left + right) * 0.5f, (top + bottom) * 0.5f};
    }
    static constexpr RectF fromXYWH(float x, float y, float w, float h) noexcept {
        return {x, y, x + w, y + h};
    }
};

struct ColorRgba {
    float r{0.f}, g{0.f}, b{0.f}, a{1.f};
    static constexpr ColorRgba fromHex(uint32_t rgb, float alpha = 1.f) noexcept {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f,
                (rgb & 0xFF) / 255.f, alpha};
    }
    static constexpr ColorRgba fromRgbaHex(uint32_t rgba) noexcept {
        return {((rgba >> 24) & 0xFF) / 255.f, ((rgba >> 16) & 0xFF) / 255.f,
                ((rgba >> 8) & 0xFF) / 255.f, (rgba & 0xFF) / 255.f};
    }
    constexpr uint32_t toRgbHex() const noexcept {
        return (static_cast<uint32_t>(r * 255.f) << 16) |
               (static_cast<uint32_t>(g * 255.f) << 8) |
               static_cast<uint32_t>(b * 255.f);
    }
};

// Lightweight multicast delegate with token-based subscription.
template <typename... Args>
class Event {
public:
    using Handler = std::function<void(Args...)>;
    int connect(Handler fn) { return add(std::move(fn)); }
    int add(Handler fn) {
        const int token = ++nextToken_;
        handlers_.emplace(token, std::move(fn));
        return token;
    }
    void remove(int token) { handlers_.erase(token); }
    void clear() { handlers_.clear(); }
    void emit(Args... args) const {
        auto copy = handlers_;
        for (auto& [_, fn] : copy) {
            if (fn) fn(args...);
        }
    }
private:
    mutable int nextToken_{0};
    std::map<int, Handler> handlers_;
};

} // namespace sc::core
