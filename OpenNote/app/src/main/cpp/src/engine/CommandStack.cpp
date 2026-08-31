#include "ob/CommandStack.h"
#include "ob/WhiteboardEngine.h"
#include <android/log.h>

#define LOG_TAG "OB_CmdStack"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace ob {

CommandStack::CommandStack(int32_t maxDepth) : m_maxDepth(maxDepth) {}

void CommandStack::push(std::unique_ptr<Command> cmd) {
    // Discard any redo branch
    if (m_cursor < (int32_t)m_history.size() - 1) {
        m_history.erase(m_history.begin() + m_cursor + 1, m_history.end());
    }
    m_history.push_back(std::move(cmd));
    m_cursor = (int32_t)m_history.size() - 1;
    trimToLimit();
    notifyChanged();
    LOGI("push cmd, history size=%zu cursor=%d", m_history.size(), m_cursor);
}

bool CommandStack::canUndo() const {
    return m_cursor >= 0;
}

bool CommandStack::canRedo() const {
    return m_cursor < (int32_t)m_history.size() - 1;
}

void CommandStack::undo() {
    if (!canUndo()) return;
    m_history[m_cursor]->undo();
    m_cursor--;
    notifyChanged();
    LOGI("undo, cursor=%d", m_cursor);
}

void CommandStack::redo() {
    if (!canRedo()) return;
    m_cursor++;
    m_history[m_cursor]->execute();
    notifyChanged();
    LOGI("redo, cursor=%d", m_cursor);
}

void CommandStack::clear() {
    m_history.clear();
    m_cursor = -1;
    notifyChanged();
}

int32_t CommandStack::undoDepth() const {
    return m_cursor + 1;
}

int32_t CommandStack::redoDepth() const {
    return (int32_t)m_history.size() - m_cursor - 1;
}

void CommandStack::trimToLimit() {
    if (m_maxDepth <= 0) return;
    while ((int32_t)m_history.size() > m_maxDepth) {
        m_history.erase(m_history.begin());
        m_cursor--;
    }
    if (m_cursor < -1) m_cursor = -1;
}

void CommandStack::notifyChanged() {
    if (onChanged) onChanged(canUndo(), canRedo());
}

// ─── AddStrokeCommand ─────────────────────────────────────────────────────────

AddStrokeCommand::AddStrokeCommand(WhiteboardEngine* engine, uint32_t pageId, const Stroke& stroke)
    : m_engine(engine), m_pageId(pageId), m_stroke(stroke) {}

void AddStrokeCommand::execute() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            pg.strokes[m_stroke.id] = m_stroke;
            pg.strokes[m_stroke.id].isErased = false;
            break;
        }
    }
    m_engine->invalidate(DIRTY_STROKES | DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void AddStrokeCommand::undo() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            auto it = pg.strokes.find(m_stroke.id);
            if (it != pg.strokes.end()) {
                it->second.isErased = true;
            }
            break;
        }
    }
    m_engine->invalidate(DIRTY_STROKES | DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

// ─── AddShapeCommand ──────────────────────────────────────────────────────────

AddShapeCommand::AddShapeCommand(WhiteboardEngine* engine, uint32_t pageId, const ShapeElement& shape)
    : m_engine(engine), m_pageId(pageId), m_shape(shape) {}

void AddShapeCommand::execute() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            bool found = false;
            for (auto& s : pg.shapes) {
                if (s.id == m_shape.id) {
                    s.isErased = false;
                    found = true;
                    break;
                }
            }
            if (!found) {
                pg.shapes.push_back(m_shape);
            }
            break;
        }
    }
    m_engine->invalidate(DIRTY_SHAPES | DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void AddShapeCommand::undo() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            for (auto& s : pg.shapes) {
                if (s.id == m_shape.id) {
                    s.isErased = true;
                    break;
                }
            }
            break;
        }
    }
    m_engine->invalidate(DIRTY_SHAPES | DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

// ─── AddElementsCommand ───────────────────────────────────────────────────────

AddElementsCommand::AddElementsCommand(WhiteboardEngine* engine, uint32_t pageId,
                                       std::vector<Stroke> strokes,
                                       std::vector<ShapeElement> shapes,
                                       std::vector<ImageElement> images)
    : m_engine(engine), m_pageId(pageId),
      m_strokes(std::move(strokes)),
      m_shapes(std::move(shapes)),
      m_images(std::move(images)) {}

void AddElementsCommand::execute() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            for (const auto& s : m_strokes) {
                pg.strokes[s.id] = s;
                pg.strokes[s.id].isErased = false;
            }
            for (const auto& sh : m_shapes) {
                bool found = false;
                for (auto& s : pg.shapes) {
                    if (s.id == sh.id) {
                        s = sh;
                        s.isErased = false;
                        found = true;
                        break;
                    }
                }
                if (!found) pg.shapes.push_back(sh);
            }
            for (const auto& img : m_images) {
                bool found = false;
                for (auto& i : pg.images) {
                    if (i.id == img.id) {
                        i = img;
                        i.isErased = false;
                        found = true;
                        break;
                    }
                }
                if (!found) pg.images.push_back(img);
            }
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void AddElementsCommand::undo() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            for (const auto& s : m_strokes) {
                auto it = pg.strokes.find(s.id);
                if (it != pg.strokes.end()) {
                    it->second.isErased = true;
                }
            }
            for (const auto& sh : m_shapes) {
                for (auto& s : pg.shapes) {
                    if (s.id == sh.id) {
                        s.isErased = true;
                        break;
                    }
                }
            }
            for (const auto& img : m_images) {
                for (auto& i : pg.images) {
                    if (i.id == img.id) {
                        i.isErased = true;
                        break;
                    }
                }
            }
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

// ─── DeleteObjectsCommand ─────────────────────────────────────────────────────

DeleteObjectsCommand::DeleteObjectsCommand(WhiteboardEngine* engine, uint32_t pageId,
                                           std::vector<uint32_t> strokeIds,
                                           std::vector<uint32_t> shapeIds,
                                           std::vector<uint32_t> imageIds)
    : m_engine(engine), m_pageId(pageId),
      m_strokeIds(std::move(strokeIds)),
      m_shapeIds(std::move(shapeIds)),
      m_imageIds(std::move(imageIds)) {}

void DeleteObjectsCommand::execute() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            for (uint32_t sid : m_strokeIds) {
                auto it = pg.strokes.find(sid);
                if (it != pg.strokes.end()) {
                    it->second.isErased = true;
                    it->second.isSelected = false;
                }
            }
            for (uint32_t shid : m_shapeIds) {
                for (auto& s : pg.shapes) {
                    if (s.id == shid) {
                        s.isErased = true;
                        s.isSelected = false;
                        break;
                    }
                }
            }
            for (uint32_t imid : m_imageIds) {
                for (auto& i : pg.images) {
                    if (i.id == imid) {
                        i.isErased = true;
                        i.isSelected = false;
                        break;
                    }
                }
            }
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void DeleteObjectsCommand::undo() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            for (uint32_t sid : m_strokeIds) {
                auto it = pg.strokes.find(sid);
                if (it != pg.strokes.end()) {
                    it->second.isErased = false;
                }
            }
            for (uint32_t shid : m_shapeIds) {
                for (auto& s : pg.shapes) {
                    if (s.id == shid) {
                        s.isErased = false;
                        break;
                    }
                }
            }
            for (uint32_t imid : m_imageIds) {
                for (auto& i : pg.images) {
                    if (i.id == imid) {
                        i.isErased = false;
                        break;
                    }
                }
            }
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

// ─── EraseCommand ─────────────────────────────────────────────────────────────

EraseCommand::EraseCommand(WhiteboardEngine* engine, uint32_t pageId,
                           std::unordered_map<uint32_t, Stroke> beforeStrokes,
                           std::unordered_map<uint32_t, Stroke> afterStrokes,
                           std::vector<ShapeElement> beforeShapes,
                           std::vector<ShapeElement> afterShapes,
                           std::vector<ImageElement> beforeImages,
                           std::vector<ImageElement> afterImages)
    : m_engine(engine), m_pageId(pageId),
      m_beforeStrokes(std::move(beforeStrokes)),
      m_afterStrokes(std::move(afterStrokes)),
      m_beforeShapes(std::move(beforeShapes)),
      m_afterShapes(std::move(afterShapes)),
      m_beforeImages(std::move(beforeImages)),
      m_afterImages(std::move(afterImages)) {}

void EraseCommand::execute() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            pg.strokes = m_afterStrokes;
            pg.shapes  = m_afterShapes;
            pg.images  = m_afterImages;
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void EraseCommand::undo() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            pg.strokes = m_beforeStrokes;
            pg.shapes  = m_beforeShapes;
            pg.images  = m_beforeImages;
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

// ─── ClearPageCommand ─────────────────────────────────────────────────────────

ClearPageCommand::ClearPageCommand(WhiteboardEngine* engine, uint32_t pageId,
                                   std::unordered_map<uint32_t, Stroke> strokes,
                                   std::vector<ShapeElement> shapes,
                                   std::vector<ImageElement> images)
    : m_engine(engine), m_pageId(pageId),
      m_strokes(std::move(strokes)),
      m_shapes(std::move(shapes)),
      m_images(std::move(images)) {}

void ClearPageCommand::execute() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            pg.strokes.clear();
            pg.shapes.clear();
            pg.images.clear();
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void ClearPageCommand::undo() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            pg.strokes = m_strokes;
            pg.shapes  = m_shapes;
            pg.images  = m_images;
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

// ─── ModifyObjectsCommand ─────────────────────────────────────────────────────

ModifyObjectsCommand::ModifyObjectsCommand(WhiteboardEngine* engine, uint32_t pageId,
                                           std::unordered_map<uint32_t, Stroke> beforeStrokes,
                                           std::unordered_map<uint32_t, Stroke> afterStrokes,
                                           std::vector<ShapeElement> beforeShapes,
                                           std::vector<ShapeElement> afterShapes,
                                           std::vector<ImageElement> beforeImages,
                                           std::vector<ImageElement> afterImages)
    : m_engine(engine), m_pageId(pageId),
      m_beforeStrokes(std::move(beforeStrokes)),
      m_afterStrokes(std::move(afterStrokes)),
      m_beforeShapes(std::move(beforeShapes)),
      m_afterShapes(std::move(afterShapes)),
      m_beforeImages(std::move(beforeImages)),
      m_afterImages(std::move(afterImages)) {}

void ModifyObjectsCommand::execute() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            pg.strokes = m_afterStrokes;
            pg.shapes  = m_afterShapes;
            pg.images  = m_afterImages;
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

void ModifyObjectsCommand::undo() {
    if (!m_engine) return;
    for (auto& pg : m_engine->document().pages) {
        if (pg.id == m_pageId) {
            pg.strokes = m_beforeStrokes;
            pg.shapes  = m_beforeShapes;
            pg.images  = m_beforeImages;
            break;
        }
    }
    m_engine->invalidate(DIRTY_ALL);
    if (m_engine->callbacks().onInvalidate) m_engine->callbacks().onInvalidate();
}

} // namespace ob
