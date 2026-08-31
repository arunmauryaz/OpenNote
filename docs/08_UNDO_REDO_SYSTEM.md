# 08 — Undo / Redo System

## Design Philosophy

Every user action in OpenBoard is represented as a **Command object** following the classic Command Pattern. This gives us:

1. **Unlimited undo/redo** — the history stack has no hard limit (configurable soft limit)
2. **Predictable behavior** — every action is reversible by design
3. **Plugin compatibility** — plugins issue commands through the same system
4. **Replay / macro** — commands can be replayed in sequence

---

## Core Classes

```cpp
// core/include/openboard/history.h

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute()   = 0;   // Perform the action
    virtual void undo()      = 0;   // Reverse the action
    virtual void redo()      = 0;   // Re-apply (default: call execute())
    virtual std::string describe() const = 0;   // Human-readable label
    virtual size_t memoryUsage() const { return 0; }   // For memory limit
    bool mergeWith(ICommand* other) { return false; }   // Command merging
};

class HistoryManager {
public:
    // Execute and push to history
    void execute(std::unique_ptr<ICommand> command);

    // Undo last command
    bool undo();

    // Redo next command
    bool redo();

    // State queries
    bool        canUndo() const;
    bool        canRedo() const;
    std::string undoDescription() const;    // "Undo Draw Stroke"
    std::string redoDescription() const;    // "Redo Delete Layer"
    int         undoStackDepth() const;
    int         redoStackDepth() const;

    // Configuration
    void        setMaxHistorySize(size_t bytes);   // Default: 512 MB
    void        setMaxCommandCount(int count);      // Default: 1000

    // Bulk operations
    void        beginGroup(const std::string& name); // Start grouped command
    void        endGroup();                          // Commit group as one command
    void        clear();                             // Clear all history

    // Notifications
    using StateChangeCallback = std::function<void()>;
    void        setStateChangeCallback(StateChangeCallback cb);
};
```

---

## All Commands (Complete List)

### Drawing Commands
| Command                  | execute()                              | undo()                            |
|--------------------------|----------------------------------------|-----------------------------------|
| `AddStrokeCommand`       | Add stroke to layer's element list     | Remove stroke from list           |
| `EraseStrokeCommand`     | Remove one or more strokes             | Restore removed strokes           |
| `SplitStrokeCommand`     | Split stroke into sub-strokes + delete middle | Restore original stroke     |
| `ModifyStrokeCommand`    | Apply transform/color change to stroke | Restore previous stroke data      |

### Selection & Transform Commands
| Command                  | execute()                              | undo()                            |
|--------------------------|----------------------------------------|-----------------------------------|
| `MoveElementsCommand`    | Translate selected elements by delta   | Translate back by -delta          |
| `ScaleElementsCommand`   | Scale selected elements                | Scale back to original            |
| `RotateElementsCommand`  | Rotate selected elements               | Rotate back                       |
| `DeleteElementsCommand`  | Remove elements from their layers      | Restore elements to layers        |
| `DuplicateElementsCommand`| Add copies to layer                   | Remove copies                     |
| `GroupElementsCommand`   | Wrap in GroupElement                   | Unwrap GroupElement               |
| `UngroupElementsCommand` | Unwrap GroupElement                    | Re-wrap in GroupElement           |

### Layer Commands
| Command                  | execute()                              | undo()                            |
|--------------------------|----------------------------------------|-----------------------------------|
| `AddLayerCommand`        | Create and insert layer                | Delete layer (preserve data)      |
| `DeleteLayerCommand`     | Delete layer (preserve data in cmd)    | Restore layer and all its elements|
| `MoveLayerCommand`       | Change layer z-index                   | Restore previous z-index          |
| `RenameLayerCommand`     | Set new name                           | Restore old name                  |
| `SetOpacityCommand`      | Set opacity value                      | Restore old opacity               |
| `SetBlendModeCommand`    | Set blend mode                         | Restore old blend mode            |
| `SetVisibilityCommand`   | Show/hide layer                        | Restore old visibility            |
| `MergeLayersCommand`     | Merge two layers                       | Restore both original layers      |

### Canvas / Document Commands
| Command                  | execute()                              | undo()                            |
|--------------------------|----------------------------------------|-----------------------------------|
| `AddPageCommand`         | Insert page at index                   | Remove page                       |
| `DeletePageCommand`      | Remove page (preserve data)            | Restore page                      |
| `MovePageCommand`        | Change page order                      | Restore page order                |
| `ResizePageCommand`      | Change page dimensions                 | Restore old dimensions            |
| `SetBackgroundCommand`   | Set page background color/image        | Restore old background            |

### Shape & Text Commands
| Command                  | execute()                              | undo()                            |
|--------------------------|----------------------------------------|-----------------------------------|
| `AddShapeCommand`        | Add shape element to layer             | Remove shape element              |
| `ModifyShapeCommand`     | Change shape properties                | Restore previous properties       |
| `AddTextCommand`         | Add text element to layer              | Remove text element               |
| `ModifyTextCommand`      | Change text content/style              | Restore previous text data        |
| `AddImageCommand`        | Add image element                      | Remove image element              |

---

## Command Merging (Coalescing)

For continuous operations like stroke drawing or moving, we don't want 200 commands for 200 touch points. Instead:

- While drawing: we do NOT push to history mid-stroke
- On TOUCH_UP: a single `AddStrokeCommand` is pushed (complete stroke)

For text editing:
- Character insertions within 2 seconds of each other are merged into a single `ModifyTextCommand`
- `mergeWith()` returns true if mergeable and updates internal state

For move operations:
- All intermediate move positions are discarded; only the final position is committed as one `MoveElementsCommand`

---

## Grouped Commands

When a single user action triggers multiple sub-commands:

```cpp
historyManager.beginGroup("Paste Elements");
    historyManager.execute(std::make_unique<AddImageCommand>(...));
    historyManager.execute(std::make_unique<AddTextCommand>(...));
    historyManager.execute(std::make_unique<SetSelectionCommand>(...));
historyManager.endGroup();
// All three commands appear as ONE undo step
```

This is used for: paste, merge layers, import, plugin operations.

---

## Memory Management

The history stack can grow large when storing deleted strokes or high-resolution images.

Strategy:
1. Each command reports `memoryUsage()` in bytes
2. HistoryManager tracks total memory used
3. When limit is exceeded, oldest commands on undo stack are dropped ("compacted")
4. User sees a visual indicator "History limit reached — some undo steps removed"

Default limit: 512 MB. Configurable per device capability.

---

## Plugin Commands

Plugins register custom commands via the Plugin SDK:

```cpp
class MyPluginCommand : public ICommand {
public:
    void execute() override { /* do plugin thing */ }
    void undo()    override { /* reverse it */ }
    std::string describe() const override { return "My Plugin Action"; }
};

// Plugin calls:
engine->getHistoryManager()->execute(std::make_unique<MyPluginCommand>(...));
```

This ensures plugin actions are fully undoable and fit naturally into the history stack.

---

## UI Integration

The UI observes the `HistoryManager` via the state change callback:

- Undo button is enabled/disabled based on `canUndo()`
- Redo button is enabled/disabled based on `canRedo()`
- Button tooltips show `undoDescription()` / `redoDescription()`
- History panel (optional) shows full list of undo stack entries

---

## Serialization

**History is NOT serialized** in `.obn` files. When a file is reopened, the undo stack starts fresh. This is by design — saving should reflect a clean "checkpoint" state.

Future consideration: `session.obn.tmp` temporary file saves mid-session history for crash recovery.
