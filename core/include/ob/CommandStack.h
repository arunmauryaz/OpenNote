#pragma once

#include "Command.h"
#include <vector>
#include <memory>
#include <functional>

namespace ob {

// ─── Command Stack (Undo/Redo) ────────────────────────────────────────────────

class CommandStack {
public:
    explicit CommandStack(int32_t maxDepth = 50);

    // Execute cmd and push to history (clears redo branch)
    void push(std::unique_ptr<Command> cmd);

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void clear();

    int32_t undoDepth() const;
    int32_t redoDepth() const;

    // Called after undo/redo to notify UI
    std::function<void(bool canUndo, bool canRedo)> onChanged;

private:
    int32_t                              m_maxDepth;
    std::vector<std::unique_ptr<Command>> m_history; // index 0 = oldest
    int32_t                              m_cursor = -1; // points to last executed

    void trimToLimit();
    void notifyChanged();
};

} // namespace ob
