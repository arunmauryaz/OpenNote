#pragma once

#include "Types.h"
#include "Elements.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace ob {

class WhiteboardEngine;

// ─── Command Interface (Command Pattern for Undo/Redo) ───────────────────────

class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo()    = 0;
    virtual uint32_t pageId() const { return 0; }
    virtual bool tryMerge(Command* /*next*/) { return false; }
    virtual const char* name() const { return "Command"; }
};

// ─── AddStrokeCommand ─────────────────────────────────────────────────────────

class AddStrokeCommand : public Command {
public:
    AddStrokeCommand(WhiteboardEngine* engine, uint32_t pageId, const Stroke& stroke);
    void execute() override;
    void undo() override;
    uint32_t pageId() const override { return m_pageId; }
    const char* name() const override { return "AddStroke"; }
private:
    WhiteboardEngine* m_engine;
    uint32_t          m_pageId;
    Stroke            m_stroke;
};

// ─── AddShapeCommand ──────────────────────────────────────────────────────────

class AddShapeCommand : public Command {
public:
    AddShapeCommand(WhiteboardEngine* engine, uint32_t pageId, const ShapeElement& shape);
    void execute() override;
    void undo() override;
    uint32_t pageId() const override { return m_pageId; }
    const char* name() const override { return "AddShape"; }
private:
    WhiteboardEngine* m_engine;
    uint32_t          m_pageId;
    ShapeElement      m_shape;
};

// ─── AddElementsCommand (Pasting, Duplicating multiple objects) ─────────────────

class AddElementsCommand : public Command {
public:
    AddElementsCommand(WhiteboardEngine* engine, uint32_t pageId,
                       std::vector<Stroke> strokes,
                       std::vector<ShapeElement> shapes,
                       std::vector<ImageElement> images);
    void execute() override;
    void undo() override;
    uint32_t pageId() const override { return m_pageId; }
    const char* name() const override { return "AddElements"; }
private:
    WhiteboardEngine*         m_engine;
    uint32_t                  m_pageId;
    std::vector<Stroke>       m_strokes;
    std::vector<ShapeElement> m_shapes;
    std::vector<ImageElement> m_images;
};

// ─── DeleteObjectsCommand (Selection deletion) ────────────────────────────────

class DeleteObjectsCommand : public Command {
public:
    DeleteObjectsCommand(WhiteboardEngine* engine, uint32_t pageId,
                         std::vector<uint32_t> strokeIds,
                         std::vector<uint32_t> shapeIds,
                         std::vector<uint32_t> imageIds);
    void execute() override;
    void undo() override;
    uint32_t pageId() const override { return m_pageId; }
    const char* name() const override { return "DeleteObjects"; }
private:
    WhiteboardEngine*     m_engine;
    uint32_t              m_pageId;
    std::vector<uint32_t> m_strokeIds;
    std::vector<uint32_t> m_shapeIds;
    std::vector<uint32_t> m_imageIds;
};

// ─── EraseCommand (Eraser tool strokes/shapes splitting & deletion) ───────────

class EraseCommand : public Command {
public:
    EraseCommand(WhiteboardEngine* engine, uint32_t pageId,
                 std::unordered_map<uint32_t, Stroke> beforeStrokes,
                 std::unordered_map<uint32_t, Stroke> afterStrokes,
                 std::vector<ShapeElement> beforeShapes,
                 std::vector<ShapeElement> afterShapes,
                 std::vector<ImageElement> beforeImages,
                 std::vector<ImageElement> afterImages);
    void execute() override;
    void undo() override;
    uint32_t pageId() const override { return m_pageId; }
    const char* name() const override { return "Erase"; }
private:
    WhiteboardEngine*                    m_engine;
    uint32_t                             m_pageId;
    std::unordered_map<uint32_t, Stroke> m_beforeStrokes;
    std::unordered_map<uint32_t, Stroke> m_afterStrokes;
    std::vector<ShapeElement>            m_beforeShapes;
    std::vector<ShapeElement>            m_afterShapes;
    std::vector<ImageElement>            m_beforeImages;
    std::vector<ImageElement>            m_afterImages;
};

// ─── ClearPageCommand (Clear entire active page) ──────────────────────────────

class ClearPageCommand : public Command {
public:
    ClearPageCommand(WhiteboardEngine* engine, uint32_t pageId,
                     std::unordered_map<uint32_t, Stroke> strokes,
                     std::vector<ShapeElement> shapes,
                     std::vector<ImageElement> images);
    void execute() override;
    void undo() override;
    uint32_t pageId() const override { return m_pageId; }
    const char* name() const override { return "ClearPage"; }
private:
    WhiteboardEngine*                    m_engine;
    uint32_t                             m_pageId;
    std::unordered_map<uint32_t, Stroke> m_strokes;
    std::vector<ShapeElement>            m_shapes;
    std::vector<ImageElement>            m_images;
};

// ─── ModifyObjectsCommand (Transforms: move, rotate, scale, flip, color) ─────

class ModifyObjectsCommand : public Command {
public:
    ModifyObjectsCommand(WhiteboardEngine* engine, uint32_t pageId,
                         std::unordered_map<uint32_t, Stroke> beforeStrokes,
                         std::unordered_map<uint32_t, Stroke> afterStrokes,
                         std::vector<ShapeElement> beforeShapes,
                         std::vector<ShapeElement> afterShapes,
                         std::vector<ImageElement> beforeImages,
                         std::vector<ImageElement> afterImages);
    void execute() override;
    void undo() override;
    uint32_t pageId() const override { return m_pageId; }
    const char* name() const override { return "ModifyObjects"; }
private:
    WhiteboardEngine*                    m_engine;
    uint32_t                             m_pageId;
    std::unordered_map<uint32_t, Stroke> m_beforeStrokes;
    std::unordered_map<uint32_t, Stroke> m_afterStrokes;
    std::vector<ShapeElement>            m_beforeShapes;
    std::vector<ShapeElement>            m_afterShapes;
    std::vector<ImageElement>            m_beforeImages;
    std::vector<ImageElement>            m_afterImages;
};

} // namespace ob
