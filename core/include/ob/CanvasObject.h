#ifndef OB_CANVAS_OBJECT_H
#define OB_CANVAS_OBJECT_H

#include "ob/Types.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace ob {

enum class ObjectType : uint8_t {
    STROKE = 0,
    SHAPE  = 1,
    IMAGE  = 2,
    TEXT   = 3
};

class CanvasObject {
public:
    virtual ~CanvasObject() = default;

    virtual uint32_t getId() const = 0;
    virtual ObjectType getType() const = 0;

    virtual Rectf getBounds() const = 0;
    virtual void setBounds(const Rectf& bounds) = 0;

    virtual bool getIsErased() const = 0;
    virtual void setIsErased(bool erased) = 0;

    virtual bool getIsSelected() const = 0;
    virtual void setIsSelected(bool selected) = 0;

    virtual bool getIsLocked() const = 0;
    virtual void setIsLocked(bool locked) = 0;

    virtual void translate(float dx, float dy) = 0;
    virtual void scale(const Rectf& initialSelectionBounds, const Rectf& newBounds, float scaleX, float scaleY) = 0;
    virtual void rotate(Vec2f pivot, float deltaAngleRad) = 0;

    virtual bool hitTest(Vec2f pt, float hitRadius) const = 0;
    virtual bool intersectsLasso(const std::vector<Vec2f>& lassoPoly, const Rectf& lassoBounds) const = 0;

    virtual Color getColor() const { return Color::black(); }
    virtual void setColor(Color c) { (void)c; }
};

} // namespace ob

#endif // OB_CANVAS_OBJECT_H
