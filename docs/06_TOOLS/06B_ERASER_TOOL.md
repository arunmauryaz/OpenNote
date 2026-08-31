# 06B — Eraser Tool Engine

## Overview

The Eraser Tool Engine handles selective element removal and pixel/path clipping. Modeled after MyViewBoard's Eraser Flyout popup, OpenBoard provides 4 distinct eraser modes and a real-time size slider.

---

## 🧹 Eraser Tools Flyout Popup (MyViewBoard Specification)

```
┌─────────────────────────────────────────────────────────────┐
│  [🧹 Standard]  [⭕ Lasso Eraser]  [〰️ Stroke]  [🗑️ Clear]  │  [═══◯════] (Size Slider)
└─────────────────────────────────────────────────────────────┘
```

---

## 🛠️ Four Eraser Modes Detail

| Icon | Mode Name | Execution Logic & Algorithm |
|------|-----------|-----------------------------|
| 🧹 | **Standard Eraser** | **Pixel / Path Clipper**: Erases ink directly under the eraser circle radius. Splits intersecting strokes into sub-strokes or subtracts pixels on raster layers. |
| ⭕ | **Lasso / Marquee Eraser** | **Lasso Area Eraser**: User draws a freehand closed loop (dashed marquee). Performs a point-in-polygon test; any stroke or element fully inside the loop is erased. |
| 〰️ | **Stroke Eraser** | **Object / Stroke Eraser**: Performs spatial R-Tree bounding query. Tapping any point of a stroke erases the entire stroke object instantaneously. |
| 🗑️ | **Clear Page** | **Page Eraser**: Instantly clears all elements and drawings on the current page (wrapped in `ClearPageCommand` for full Undo/Redo recovery). |

---

## 🎚️ Eraser Radius Slider (1px to 200px)

- Configures the circular eraser brush radius \(R\).
- Dynamic Skia Cursor Overlay: Renders a dashed gray circle indicator corresponding to the exact screen pixel radius of \(R \cdot Z_{zoom}\).
