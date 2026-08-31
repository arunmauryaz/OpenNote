# 04 — Canvas System & Infinite Minimap Navigation

## Overview

The Canvas System supports both **Fixed 16:9 Page Slide Mode** (default) and **Infinite Free-Movement Canvas Mode**. When operating in Infinite Canvas Mode, OpenBoard provides two specialized navigation tools to prevent getting lost in unbounded coordinate space:

1. **Hand Tool (`H` Keyboard Shortcut)**: Single-finger canvas pan tool, dynamically enabled when operating in Open Canvas Mode.
2. **Interactive Minimap Overview Window (`🗺️`)**: Floating thumbnail overview box displaying all drawn elements on the infinite plane with tap-to-teleport and drag-to-pan functionality.

---

## 🗺️ Infinite Canvas Minimap Overview Window (Screenshot Specification)

```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│                MINIMAP OVERVIEW WINDOW                       │
│    ┌────────────────────────────────────────────────────┐    │
│    │               [Drawn Elements Bounding Box]        │    │
│    │                                                    │    │
│    │              ┌──────────────────────┐              │    │
│    │              │ Active Viewport Rect │              │    │
│    │              │ (Current Screen View)│              │    │
│    │              └──────────────────────┘              │    │
│    │                                                    │    │
│    └────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────┐
│  >  │ [🗺️ Minimap]  │ [🔍 Fit]  │ [− 100% +] │ [👤 Profile]    │
└──────────────────────────────────────────────────────────────┘
```

### Minimap Capabilities & C++ Engine Logic

- **Thumbnail Rendering**: Skia renders an offscreen low-resolution composite of all layer elements' global bounding box \(B_{total} = \bigcup \text{ElementBounds}\).
- **Viewport Indicator Box**: Renders a highlighted rectangular outline representing the active camera viewport coordinates transformed into minimap scale:

$$Rect_{minimap} = \text{MapToMinimap}(X_{camera}, Y_{camera}, \text{ViewportWidth}, \text{ViewportHeight})$$

- **Tap-to-Teleport**: Tapping any coordinate on the Minimap calculates the corresponding World Space coordinate and smoothly animates `CanvasCamera` to center over that point instantly.
- **Drag-to-Pan**: Dragging the Viewport Indicator Box inside the Minimap dynamically updates `CanvasCamera::offsetX/offsetY` in real-time.

---

## ✋ Hand Tool (`H`) Mode-Dependent Visibility

- **Open Canvas Mode**: The **Hand Tool (`✋`)** is displayed prominently on the primary bottom toolbar (shortcut `H`). When selected, single-finger touch drags translate the canvas camera, ignoring drawing strokes.
- **Page Slide Mode**: The Hand Tool icon is automatically hidden or replaced by page navigation controls (`< 1/12 >`), as panning is constrained to page boundaries.
