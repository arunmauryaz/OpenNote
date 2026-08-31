#ifndef OB_TOOLS_H
#define OB_TOOLS_H

#include "ob/Types.h"
#include "ob/Elements.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace ob {

class WhiteboardEngine;

enum class ActiveTool {
    PEN,
    BRUSH,
    HIGHLIGHTER,
    ERASER,
    LASER,
    SELECTION,
    HAND,
    SHAPE
};

class BaseTool {
public:
    virtual ~BaseTool() = default;
    virtual void onBegan(Vec2f pt, float pressure) = 0;
    virtual void onMoved(Vec2f pt, float pressure) = 0;
    virtual void onEnded(Vec2f pt) = 0;
    virtual void onCancelled() = 0;
};

class PenTool : public BaseTool {
public:
    explicit PenTool(WhiteboardEngine* engine);
    void setStyle(StrokeStyle s) { m_style = s; }
    StrokeStyle style() const { return m_style; }

    void onBegan(Vec2f pt, float pressure) override;
    void onMoved(Vec2f pt, float pressure) override;
    void onEnded(Vec2f pt) override;
    void onCancelled() override;

    void setPenType(uint8_t penType) { m_style.penType = penType; }

private:
    WhiteboardEngine*        m_engine;
    StrokeStyle              m_style;
    Stroke                   m_currentStroke;
    std::vector<StrokePoint> m_rawPoints;

    // ── Incremental Build State ───────────────────────────────────────────
    // Points committed to m_currentStroke.points that will never change again.
    // Only the last ~3 raw points contribute to the mutable live tail.
    size_t m_committedPointCount = 0;

    // When true, the last point in m_currentStroke.points is a predicted
    // (extrapolated) ghost tip, NOT a real sample. It is replaced as soon as
    // the next real touch event arrives.
    bool   m_hasPredictedTip = false;

    // Full O(n) rebuild — called only on stroke END for final cleanup.
    void rebuildStroke();
    // Incremental O(1) append — called on every MOVE for real-time path.
    void appendIncrementalSegment();
};

enum class EraserMode : uint8_t {
    NORMAL = 0,  // Paint-style circular area eraser (with partial stroke splitting)
    OBJECT = 1   // Whole object/stroke deletion on touch
};

class EraserTool : public BaseTool {
public:
    explicit EraserTool(WhiteboardEngine* engine);
    void setRadius(float r) { m_radius = r; }
    float radius() const { return m_radius; }

    void setMode(EraserMode mode) { m_mode = mode; }
    EraserMode mode() const { return m_mode; }

    void onBegan(Vec2f pt, float pressure) override;
    void onMoved(Vec2f pt, float pressure) override;
    void onEnded(Vec2f pt) override;
    void onCancelled() override;

private:
    WhiteboardEngine*                    m_engine;
    float                                m_radius = 25.0f;
    EraserMode                           m_mode = EraserMode::NORMAL;
    Vec2f                                m_lastPt{0.f, 0.f};
    std::unordered_map<uint32_t, Stroke> m_beforeStrokes;
    std::vector<ShapeElement>            m_beforeShapes;
    std::vector<ImageElement>            m_beforeImages;
    bool                                 m_isErasing = false;

    void eraseAt(Vec2f pt);
    void eraseLine(Vec2f from, Vec2f to);
};

enum class SelectionHandle {
    NONE = 0,
    TOP_LEFT,
    TOP_CENTER,
    TOP_RIGHT,
    MIDDLE_RIGHT,
    BOTTOM_RIGHT,
    BOTTOM_CENTER,
    BOTTOM_LEFT,
    MIDDLE_LEFT,
    ROTATE,
    ROTATE_3D_GIMBAL,
    BODY
};

class SelectionTool : public BaseTool {
public:
    explicit SelectionTool(WhiteboardEngine* engine);
    void clearSelection();
    void deleteSelected();
    void duplicateSelected();
    void copySelected();
    void paste();
    void pasteAt(Vec2f targetCenter);
    bool hasClipboard() const;
    void lockSelected();
    void setSelectedColor(Color c);
    Color getSelectedColor() const;
    void selectSingleShape(uint32_t shapeId);
    void translateSelected(float dx, float dy);
    void scaleSelected(SelectionHandle handle, Vec2f pt);
    void rotateSelected(float deltaAngleRad);
    void rotate3DSelected(float deltaPitch, float deltaYaw);
    bool is3DSelection() const;
    void toggleFillSelected();
    bool hasSelectedShape() const;
    bool hasSelectedFilledShape() const;
    void flipHorizontalSelected();
    void flipVerticalSelected();
    void bringToFrontSelected();
    void sendBackwardSelected();
    void sendToBackSelected();
    void bringForwardSelected();
    SelectionHandle hitTestHandle(Vec2f pt) const;
    /** Tap-to-select: hit-test all strokes/shapes at canvas point. Returns true if any object was selected. */
    bool tapSelectAt(Vec2f canvasPt);

    void onBegan(Vec2f pt, float pressure) override;
    void onMoved(Vec2f pt, float pressure) override;
    void onEnded(Vec2f pt) override;
    void onCancelled() override;

    const std::vector<Vec2f>& lassoPath() const { return m_lassoPath; }
    bool isSelecting() const { return m_isSelecting; }
    const Rectf& selectionBounds() const { return m_selectionBounds; }
    float rotation() const { return m_rotation; }
    bool hasSelection() const { return m_hasSelection; }
    bool isLocked() const;

private:
    WhiteboardEngine*    m_engine;
    std::vector<Vec2f>   m_lassoPath;
    bool                                m_isSelecting = false;
    bool                                m_hasSelection = false;
    SelectionHandle                     m_activeHandle = SelectionHandle::NONE;
    Vec2f                               m_lastDragPt;
    Vec2f                               m_initialTouchPt;
    Rectf                               m_selectionBounds;
    Rectf                               m_initialSelectionBounds;
    float                               m_rotation = 0.0f;
    float                               m_initialRotation = 0.0f;
    std::unordered_map<uint32_t, Stroke>       m_initialStrokes;
    std::unordered_map<uint32_t, ShapeElement> m_initialShapes;
    std::unordered_map<uint32_t, ImageElement> m_initialImages;
    std::vector<Stroke>                        m_clipboardStrokes;
    std::vector<ShapeElement>                  m_clipboardShapes;
    std::vector<ImageElement>                  m_clipboardImages;
    int                                        m_pasteCount = 0;

    void performLassoSelection();
    void notifySelectionChanged();
};

class HandTool : public BaseTool {
public:
    explicit HandTool(WhiteboardEngine* engine);
    void onBegan(Vec2f pt, float pressure) override;
    void onMoved(Vec2f pt, float pressure) override;
    void onEnded(Vec2f pt) override;
    void onCancelled() override;
private:
    WhiteboardEngine* m_engine;
    Vec2f m_lastPt;
};

class ShapeTool : public BaseTool {
public:
    explicit ShapeTool(WhiteboardEngine* engine);

    void setShapeType(ShapeType type) { m_shapeType = type; }
    ShapeType shapeType() const { return m_shapeType; }

    void setStrokeColor(Color c) { m_strokeColor = c; }
    void setStrokeWidth(float w) { m_strokeWidth = w; }
    void setFillColor(Color c) { m_fillColor = c; }

    void onBegan(Vec2f pt, float pressure) override;
    void onMoved(Vec2f pt, float pressure) override;
    void onEnded(Vec2f pt) override;
    void onCancelled() override;

    bool isDrawing() const { return m_isDrawing; }
    const ShapeElement* liveShape() const { return m_isDrawing ? &m_liveShape : nullptr; }

private:
    WhiteboardEngine* m_engine;
    ShapeType         m_shapeType = ShapeType::RECTANGLE;
    Color             m_strokeColor = Color::black();
    Color             m_fillColor = Color::transparent();
    float             m_strokeWidth = 3.0f;
    bool              m_isDrawing = false;
    Vec2f             m_startPt{0.f, 0.f};
    ShapeElement      m_liveShape;
};

class ToolManager {
public:
    explicit ToolManager(WhiteboardEngine* engine);

    void setActiveTool(ActiveTool tool);
    ActiveTool activeTool() const { return m_activeTool; }

    PenTool* penTool() { return m_penTool.get(); }
    EraserTool* eraserTool() { return m_eraserTool.get(); }
    SelectionTool* selectionTool() { return m_selectionTool.get(); }
    HandTool* handTool() { return m_handTool.get(); }
    ShapeTool* shapeTool() { return m_shapeTool.get(); }
    WhiteboardEngine* engine() const { return m_engine; }

    void onTouchBegan(Vec2f pt, float pressure);
    void onTouchMoved(Vec2f pt, float pressure);
    void onTouchEnded(Vec2f pt);
    void onTouchCancelled();

private:
    WhiteboardEngine*              m_engine;
    ActiveTool                     m_activeTool = ActiveTool::PEN;
    std::unique_ptr<PenTool>       m_penTool;
    std::unique_ptr<EraserTool>    m_eraserTool;
    std::unique_ptr<SelectionTool> m_selectionTool;
    std::unique_ptr<HandTool>      m_handTool;
    std::unique_ptr<ShapeTool>     m_shapeTool;
};

} // namespace ob

#endif // OB_TOOLS_H
