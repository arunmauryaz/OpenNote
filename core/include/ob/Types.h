#ifndef OB_TYPES_H
#define OB_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>

namespace ob {

using ElementId = uint64_t;
using PageId    = uint64_t;
using LayerId   = uint32_t;

constexpr ElementId INVALID_ELEMENT_ID = 0;
constexpr PageId    INVALID_PAGE_ID    = 0;
constexpr LayerId   INVALID_LAYER_ID   = 0;

enum DirtyFlags : uint32_t {
    DIRTY_NONE       = 0,
    DIRTY_STROKES    = 1 << 0,
    DIRTY_BACKGROUND = 1 << 1,
    DIRTY_SHAPES     = 1 << 2,
    DIRTY_IMAGES     = 1 << 3,
    DIRTY_CAMERA     = 1 << 4,
    DIRTY_UI         = 1 << 5,
    DIRTY_ALL        = 0xFFFFFFFF
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    static Color black()       { return Color{0, 0, 0, 255}; }
    static Color white()       { return Color{255, 255, 255, 255}; }
    static Color transparent() { return Color{0, 0, 0, 0}; }

    static Color fromARGB(uint32_t argb) {
        Color c;
        c.a = (argb >> 24) & 0xFF;
        c.r = (argb >> 16) & 0xFF;
        c.g = (argb >> 8)  & 0xFF;
        c.b = (argb)       & 0xFF;
        return c;
    }
};

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rectf {
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;

    float width()  const { return std::abs(right - left); }
    float height() const { return std::abs(bottom - top); }
    bool intersects(const Rectf& o) const {
        return !(left > o.right || right < o.left || top > o.bottom || bottom < o.top);
    }
};

} // namespace ob

#endif // OB_TYPES_H
