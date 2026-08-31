# 07 — Layer System

## Overview

The Layer System in OpenBoard works identically to layers in professional tools like Photoshop or Procreate. Every visual element on the whiteboard belongs to exactly one layer. Layers are stacked in a **z-ordered list** and composited top-to-bottom to produce the final image.

**Key principle:** Layers are managed 100% in the C++ engine. The UI only sends commands and displays the current state snapshot.

---

## Layer Data Model

```cpp
// core/include/openboard/layers.h

enum class LayerBlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion
};

struct LayerState {
    uint32_t    id;             // Unique layer ID
    std::string name;           // Display name
    bool        visible;        // Is this layer visible?
    bool        locked;         // Prevent edits when true
    float       opacity;        // 0.0 – 1.0
    LayerBlendMode blendMode;
    bool        isBackground;   // Special background layer (not deletable)
};

class Layer {
public:
    LayerState                    state;
    std::vector<ElementHandle>    elements;   // All elements on this layer
    sk_sp<SkSurface>              surface;    // GPU-cached composite of this layer
    bool                          dirty;      // Needs re-render?

    void addElement(ElementHandle handle);
    void removeElement(ElementHandle handle);
    void moveElementToTop(ElementHandle handle);
    void moveElementToBottom(ElementHandle handle);
    BoundingBox getBounds() const;
    void invalidate();          // Mark dirty, force re-render next frame
};

class LayerStack {
public:
    // CRUD
    LayerHandle createLayer(const std::string& name = "Layer");
    void        deleteLayer(LayerHandle handle);
    void        duplicateLayer(LayerHandle handle);
    void        mergeLayerDown(LayerHandle handle);
    void        mergeAllVisible();
    void        flattenToSingleLayer();

    // Ordering
    void        moveLayerUp(LayerHandle handle);
    void        moveLayerDown(LayerHandle handle);
    void        moveLayerToIndex(LayerHandle handle, int index);
    int         getLayerIndex(LayerHandle handle) const;

    // Active layer
    void        setActiveLayer(LayerHandle handle);
    LayerHandle getActiveLayer() const;

    // State
    void        setVisibility(LayerHandle handle, bool visible);
    void        setLocked(LayerHandle handle, bool locked);
    void        setOpacity(LayerHandle handle, float opacity);
    void        setBlendMode(LayerHandle handle, LayerBlendMode mode);
    void        renameLayer(LayerHandle handle, const std::string& name);

    // Query
    LayerHandle findLayerById(uint32_t id) const;
    std::vector<LayerState> getLayerStates() const;  // Snapshot for UI
    int         getLayerCount() const;

    // Compositing
    void        composite(SkCanvas* canvas, const ViewportRect& viewport);
};
```

---

## Layer Types

| Layer Type        | Description                                            |
|-------------------|--------------------------------------------------------|
| Normal Layer      | Default — contains strokes, shapes, text, images       |
| Background Layer  | Always at bottom, cannot be deleted, usually white     |
| Group Layer       | Container for other layers, collapses in panel         |
| Reference Layer   | Low-opacity locked layer used as tracing reference     |

---

## Element Ownership

Every element (stroke, shape, text, image) is owned by exactly one layer.

```
Document
└── Page 1
    ├── LayerStack
    │   ├── Layer 3 "Annotations"   ← active
    │   │   ├── StrokeElement #45
    │   │   └── TextElement #22
    │   ├── Layer 2 "Diagram"
    │   │   ├── ShapeElement #10
    │   │   └── ImageElement #11
    │   └── Layer 1 "Background"    ← isBackground = true
    │       └── (white fill, no elements)
    └── (next page)
```

---

## Compositing Pipeline

Each frame, the renderer composites layers from bottom to top:

```
for each Layer (bottom → top):
    if !layer.visible: skip
    if layer.dirty:
        re-render layer to layer.surface (SkSurface)
        layer.dirty = false
    composite layer.surface onto output canvas:
        paint.setAlpha(layer.opacity * 255)
        paint.setBlendMode(toSkBlendMode(layer.blendMode))
        canvas->drawImage(layer.surface->makeImageSnapshot(), 0, 0, &paint)
```

GPU-side compositing means each layer is a texture on the GPU. Compositing is cheap even with 30+ layers.

---

## Layer Panel UI Connection

The UI displays a list of `LayerState` snapshots (name, visible, locked, opacity, blendMode, thumbnail).

Thumbnails are generated asynchronously by the engine on a background thread at 64x64 pixels.

**UI actions → engine commands:**

| UI Action                    | Engine Command                                |
|------------------------------|-----------------------------------------------|
| Tap layer                    | `setActiveLayer(id)`                          |
| Tap eye icon                 | `setVisibility(id, !current)`                 |
| Tap lock icon                | `setLocked(id, !current)`                     |
| Drag layer in panel          | `moveLayerToIndex(id, newIndex)`              |
| Swipe delete                 | `deleteLayer(id)` wrapped in `DeleteLayerCmd` |
| Tap + button                 | `createLayer()`` wrapped in `AddLayerCmd`     |
| Long press → Duplicate       | `duplicateLayer(id)`                          |
| Long press → Merge Down      | `mergeLayerDown(id)`                          |
| Opacity slider               | `setOpacity(id, value)`                       |

All commands go through the `HistoryManager` for undo/redo.

---

## Undo/Redo for Layers

Layer operations are full commands:

- `AddLayerCommand(name)` — undo: delete the layer
- `DeleteLayerCommand(id)` — undo: restore layer with all elements
- `MoveLayerCommand(id, oldIndex, newIndex)` — undo: move back
- `SetVisibilityCommand(id, before, after)`
- `SetOpacityCommand(id, before, after)`
- `MergeLayerCommand(id, snapshotBefore)` — undo: restore both original layers

---

## Locking Behavior

When a layer is **locked**:
- All input events targeting that layer are ignored
- Elements on the layer cannot be selected, moved, or edited
- The layer still renders normally
- A lock icon is shown in the layer thumbnail in the UI

---

## Group Layers

Group layers contain other layers (including other groups). This allows:
- Applying a single opacity/blend mode to multiple layers
- Collapsing groups in the panel for organization
- Clipping masks within groups

```
Group "Chapter 1"
├── Layer "Text"
├── Layer "Drawings"
└── Layer "Images"
```

Group compositing: composite all child layers to a temp surface first, then apply group's opacity/blend mode to that surface.

---

## Serialization

Layers are serialized in the `.obn` file format as part of the page structure. See `09_FILE_FORMAT.md` for the binary layout. Each layer stores its `LayerState` plus the serialized element list. GPU surfaces are never serialized — they are rebuilt on load.
