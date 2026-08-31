#ifndef OB_ELEMENTS_H
#define OB_ELEMENTS_H

#include "ob/Types.h"
#include "ob/CanvasObject.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ob {

inline bool pointInLassoPolygon(Vec2f pt, const std::vector<Vec2f>& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > pt.y) != (poly[j].y > pt.y)) &&
            (pt.x < (poly[j].x - poly[i].x) * (pt.y - poly[i].y) / (poly[j].y - poly[i].y + 1e-6f) + poly[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

enum class ToolType : int8_t {
    SELECTION      = -1,
    HAND           = -2,
    SHAPE          = -3,
    PEN            = 0,
    BRUSH          = 1,
    HIGHLIGHTER    = 2,
    ERASER_PIXEL   = 3,
    ERASER_STROKE  = 4,
    LASER          = 5,
    STAMP          = 6,
    AI_RECOGNIZER  = 7,
    FOUNTAIN       = 8,
    NEON           = 9
};

enum class ShapeType : int32_t {
    // 2D Shapes (0 .. 99)
    LINE           = 0,
    ARROW          = 1,
    RECTANGLE      = 2,
    CIRCLE         = 3,
    TRIANGLE       = 4,
    RIGHT_TRIANGLE = 5,
    DIAMOND        = 6,
    STAR           = 7,
    HEXAGON        = 8,

    // 3D Shapes (100 .. 199)
    CUBE           = 100,
    CUBOID         = 101,
    SPHERE         = 102,
    CYLINDER       = 103,
    CONE           = 104,
    FRUSTUM        = 105,
    PYRAMID        = 106,
    PRISM          = 107
};

inline bool is3DShape(int32_t type) {
    return type >= 100 && type <= 199;
}

struct StrokePoint {
    float x        = 0.0f;
    float y        = 0.0f;
    float pressure = 1.0f;
    float tiltX    = 0.0f;
    float tiltY    = 0.0f;
    int64_t timestamp = 0;
};

struct StrokeStyle {
    Color color   = Color::black();
    float width   = 4.0f;
    float opacity = 1.0f;
    uint8_t penType = 0; // 0=Standard Pen, 1=Fountain/Calligraphy, 2=Highlighter, 3=Neon Glow
};

struct Stroke : public CanvasObject {
    uint32_t id = 0;
    ElementId elementId = INVALID_ELEMENT_ID;
    PageId    pageId    = INVALID_PAGE_ID;
    LayerId   layerId   = INVALID_LAYER_ID;
    StrokeStyle style;
    std::vector<StrokePoint> points;
    Rectf bounds;
    bool isErased = false;
    bool isSelected = false;
    bool isLocked = false;

    // CanvasObject interface
    uint32_t getId() const override { return id; }
    ObjectType getType() const override { return ObjectType::STROKE; }
    Rectf getBounds() const override { return bounds; }
    void setBounds(const Rectf& b) override { bounds = b; }
    bool getIsErased() const override { return isErased; }
    void setIsErased(bool e) override { isErased = e; }
    bool getIsSelected() const override { return isSelected; }
    void setIsSelected(bool s) override { isSelected = s; }
    bool getIsLocked() const override { return isLocked; }
    void setIsLocked(bool l) override { isLocked = l; }

    void updateBounds() {
        if (points.empty()) { bounds = Rectf{}; return; }
        float minX = points[0].x, maxX = points[0].x;
        float minY = points[0].y, maxY = points[0].y;
        for (const auto& p : points) {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }
        float pad = style.width * 0.5f;
        bounds = Rectf{minX - pad, minY - pad, maxX + pad, maxY + pad};
    }

    void translate(float dx, float dy) override {
        for (auto& p : points) {
            p.x += dx;
            p.y += dy;
        }
        bounds.left   += dx;
        bounds.right  += dx;
        bounds.top    += dy;
        bounds.bottom += dy;
    }

    void scale(const Rectf& initialBounds, const Rectf& newBounds, float scaleX, float scaleY) override {
        float sMinX = 1e9f, sMinY = 1e9f, sMaxX = -1e9f, sMaxY = -1e9f;
        for (auto& p : points) {
            p.x = newBounds.left + (p.x - initialBounds.left) * scaleX;
            p.y = newBounds.top  + (p.y - initialBounds.top)  * scaleY;
            if (p.x < sMinX) sMinX = p.x;
            if (p.x > sMaxX) sMaxX = p.x;
            if (p.y < sMinY) sMinY = p.y;
            if (p.y > sMaxY) sMaxY = p.y;
        }
        bounds = {sMinX, sMinY, sMaxX, sMaxY};
    }

    void rotate(Vec2f pivot, float deltaAngleRad) override {
        float cosA = std::cos(deltaAngleRad);
        float sinA = std::sin(deltaAngleRad);
        float sMinX = 1e9f, sMinY = 1e9f, sMaxX = -1e9f, sMaxY = -1e9f;
        for (auto& p : points) {
            float dx = p.x - pivot.x;
            float dy = p.y - pivot.y;
            p.x = pivot.x + dx * cosA - dy * sinA;
            p.y = pivot.y + dx * sinA + dy * cosA;
            if (p.x < sMinX) sMinX = p.x;
            if (p.x > sMaxX) sMaxX = p.x;
            if (p.y < sMinY) sMinY = p.y;
            if (p.y > sMaxY) sMaxY = p.y;
        }
        bounds = {sMinX, sMinY, sMaxX, sMaxY};
    }

    bool hitTest(Vec2f pt, float hitRadius) const override {
        if (isErased || points.empty()) return false;
        if (pt.x < bounds.left - hitRadius || pt.x > bounds.right + hitRadius ||
            pt.y < bounds.top  - hitRadius || pt.y > bounds.bottom + hitRadius) {
            return false;
        }
        float rSq = hitRadius * hitRadius;
        if (points.size() == 1) {
            float dx = pt.x - points[0].x, dy = pt.y - points[0].y;
            return (dx*dx + dy*dy <= rSq);
        }
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            Vec2f a{points[i].x, points[i].y}, b{points[i+1].x, points[i+1].y};
            Vec2f ab{b.x - a.x, b.y - a.y};
            float abLenSq = ab.x*ab.x + ab.y*ab.y;
            float t = 0.0f;
            if (abLenSq > 0.0001f) {
                t = ((pt.x - a.x)*ab.x + (pt.y - a.y)*ab.y) / abLenSq;
                t = std::max(0.0f, std::min(1.0f, t));
            }
            float cx = a.x + t*ab.x, cy = a.y + t*ab.y;
            float dx = pt.x - cx, dy = pt.y - cy;
            if (dx*dx + dy*dy <= rSq) return true;
        }
        return false;
    }

    bool intersectsLasso(const std::vector<Vec2f>& lassoPoly, const Rectf& lassoBounds) const override {
        if (isErased || points.empty() || !lassoBounds.intersects(bounds)) return false;
        for (const auto& p : points) {
            if (pointInLassoPolygon({p.x, p.y}, lassoPoly)) return true;
        }
        return false;
    }

    Color getColor() const override { return style.color; }
    void setColor(Color c) override { style.color = c; }
};

struct ShapeElement : public CanvasObject {
    uint32_t  id        = 0;
    ElementId elementId = INVALID_ELEMENT_ID;
    PageId    pageId    = INVALID_PAGE_ID;
    LayerId   layerId   = INVALID_LAYER_ID;
    Rectf     bounds;
    Color     strokeColor = Color::black();
    Color     fillColor   = Color::transparent();
    float     strokeWidth = 3.0f;
    int32_t   shapeType   = 2; // Default to RECTANGLE
    float     rotation    = 0.0f;
    float     rot3DX      = 0.45f; // 3D Pitch (tilt forward/back)
    float     rot3DY      = 0.55f; // 3D Yaw (spin left/right)
    float     rot3DZ      = 0.0f;  // 3D Roll
    bool      flipH       = false;
    bool      flipV       = false;
    bool      isErased    = false;
    bool      isSelected  = false;
    bool      isLocked    = false;

    // CanvasObject interface
    uint32_t getId() const override { return id; }
    ObjectType getType() const override { return ObjectType::SHAPE; }
    Rectf getBounds() const override { return bounds; }
    void setBounds(const Rectf& b) override { bounds = b; }
    bool getIsErased() const override { return isErased; }
    void setIsErased(bool e) override { isErased = e; }
    bool getIsSelected() const override { return isSelected; }
    void setIsSelected(bool s) override { isSelected = s; }
    bool getIsLocked() const override { return isLocked; }
    void setIsLocked(bool l) override { isLocked = l; }

    void translate(float dx, float dy) override {
        bounds.left   += dx;
        bounds.right  += dx;
        bounds.top    += dy;
        bounds.bottom += dy;
    }

    void scale(const Rectf& initialBounds, const Rectf& newBounds, float scaleX, float scaleY) override {
        bounds.left   = newBounds.left + (bounds.left   - initialBounds.left) * scaleX;
        bounds.right  = newBounds.left + (bounds.right  - initialBounds.left) * scaleX;
        bounds.top    = newBounds.top  + (bounds.top    - initialBounds.top)  * scaleY;
        bounds.bottom = newBounds.top  + (bounds.bottom - initialBounds.top)  * scaleY;
    }

    void rotate(Vec2f pivot, float deltaAngleRad) override {
        rotation = std::fmod(rotation + deltaAngleRad, 6.28318530718f);

        float cx = (bounds.left + bounds.right) * 0.5f;
        float cy = (bounds.top + bounds.bottom) * 0.5f;
        float hw = (bounds.right - bounds.left) * 0.5f;
        float hh = (bounds.bottom - bounds.top) * 0.5f;

        float cosA = std::cos(deltaAngleRad);
        float sinA = std::sin(deltaAngleRad);

        float dx = cx - pivot.x;
        float dy = cy - pivot.y;
        float newCx = pivot.x + dx * cosA - dy * sinA;
        float newCy = pivot.y + dx * sinA + dy * cosA;

        bounds.left   = newCx - hw;
        bounds.right  = newCx + hw;
        bounds.top    = newCy - hh;
        bounds.bottom = newCy + hh;
    }

    bool hitTest(Vec2f pt, float hitRadius) const override {
        if (isErased) return false;
        float cx = (bounds.left + bounds.right) * 0.5f;
        float cy = (bounds.top + bounds.bottom) * 0.5f;
        float hw = std::abs(bounds.right - bounds.left) * 0.5f;
        float hh = std::abs(bounds.bottom - bounds.top) * 0.5f;

        float cosA = std::cos(-rotation);
        float sinA = std::sin(-rotation);
        float dx = pt.x - cx;
        float dy = pt.y - cy;
        float lx = dx * cosA - dy * sinA;
        float ly = dx * sinA + dy * cosA;

        return (std::abs(lx) <= hw + hitRadius && std::abs(ly) <= hh + hitRadius);
    }

    bool intersectsLasso(const std::vector<Vec2f>& lassoPoly, const Rectf& lassoBounds) const override {
        if (isErased || !lassoBounds.intersects(bounds)) return false;
        Vec2f corners[5] = {
            {bounds.left, bounds.top},
            {bounds.right, bounds.top},
            {bounds.right, bounds.bottom},
            {bounds.left, bounds.bottom},
            {(bounds.left + bounds.right)*0.5f, (bounds.top + bounds.bottom)*0.5f}
        };
        for (const auto& c : corners) {
            if (pointInLassoPolygon(c, lassoPoly)) return true;
        }
        return false;
    }

    Color getColor() const override { return strokeColor; }
    void setColor(Color c) override { strokeColor = c; }
};

struct ImageElement : public CanvasObject {
    uint32_t  id        = 0;
    ElementId elementId = INVALID_ELEMENT_ID;
    PageId    pageId    = INVALID_PAGE_ID;
    LayerId   layerId   = INVALID_LAYER_ID;
    std::string imagePath;
    uint32_t  textureId = 0;
    Rectf     bounds;
    float     opacity   = 1.0f;
    float     rotation  = 0.0f;
    bool      flipH     = false;
    bool      flipV     = false;
    bool      isErased  = false;
    bool      isSelected = false;
    bool      isLocked  = false;

    // CanvasObject interface
    uint32_t getId() const override { return id; }
    ObjectType getType() const override { return ObjectType::IMAGE; }
    Rectf getBounds() const override { return bounds; }
    void setBounds(const Rectf& b) override { bounds = b; }
    bool getIsErased() const override { return isErased; }
    void setIsErased(bool e) override { isErased = e; }
    bool getIsSelected() const override { return isSelected; }
    void setIsSelected(bool s) override { isSelected = s; }
    bool getIsLocked() const override { return isLocked; }
    void setIsLocked(bool l) override { isLocked = l; }

    void translate(float dx, float dy) override {
        bounds.left   += dx;
        bounds.right  += dx;
        bounds.top    += dy;
        bounds.bottom += dy;
    }

    void scale(const Rectf& initialBounds, const Rectf& newBounds, float scaleX, float scaleY) override {
        bounds.left   = newBounds.left + (bounds.left   - initialBounds.left) * scaleX;
        bounds.right  = newBounds.left + (bounds.right  - initialBounds.left) * scaleX;
        bounds.top    = newBounds.top  + (bounds.top    - initialBounds.top)  * scaleY;
        bounds.bottom = newBounds.top  + (bounds.bottom - initialBounds.top)  * scaleY;
    }

    void rotate(Vec2f pivot, float deltaAngleRad) override {
        rotation = std::fmod(rotation + deltaAngleRad, 6.28318530718f);

        float cx = (bounds.left + bounds.right) * 0.5f;
        float cy = (bounds.top + bounds.bottom) * 0.5f;
        float hw = (bounds.right - bounds.left) * 0.5f;
        float hh = (bounds.bottom - bounds.top) * 0.5f;

        float cosA = std::cos(deltaAngleRad);
        float sinA = std::sin(deltaAngleRad);

        float dx = cx - pivot.x;
        float dy = cy - pivot.y;
        float newCx = pivot.x + dx * cosA - dy * sinA;
        float newCy = pivot.y + dx * sinA + dy * cosA;

        bounds.left   = newCx - hw;
        bounds.right  = newCx + hw;
        bounds.top    = newCy - hh;
        bounds.bottom = newCy + hh;
    }

    bool hitTest(Vec2f pt, float hitRadius) const override {
        if (isErased) return false;
        float cx = (bounds.left + bounds.right) * 0.5f;
        float cy = (bounds.top + bounds.bottom) * 0.5f;
        float hw = std::abs(bounds.right - bounds.left) * 0.5f;
        float hh = std::abs(bounds.bottom - bounds.top) * 0.5f;

        float cosA = std::cos(-rotation);
        float sinA = std::sin(-rotation);
        float dx = pt.x - cx;
        float dy = pt.y - cy;
        float lx = dx * cosA - dy * sinA;
        float ly = dx * sinA + dy * cosA;

        return (std::abs(lx) <= hw + hitRadius && std::abs(ly) <= hh + hitRadius);
    }

    bool intersectsLasso(const std::vector<Vec2f>& lassoPoly, const Rectf& lassoBounds) const override {
        if (isErased || !lassoBounds.intersects(bounds)) return false;
        Vec2f corners[5] = {
            {bounds.left, bounds.top},
            {bounds.right, bounds.top},
            {bounds.right, bounds.bottom},
            {bounds.left, bounds.bottom},
            {(bounds.left + bounds.right)*0.5f, (bounds.top + bounds.bottom)*0.5f}
        };
        for (const auto& c : corners) {
            if (pointInLassoPolygon(c, lassoPoly)) return true;
        }
        return false;
    }
};

} // namespace ob

#endif // OB_ELEMENTS_H
