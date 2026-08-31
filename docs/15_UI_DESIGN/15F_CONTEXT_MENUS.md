# 15F — Selection Floating Action Bar Specification

## Overview

When single or multiple elements are selected via the Selection or Lasso tool, a floating **Selection Context Action Bar** appears positioned cleanly above the selection bounding box (modeled on MyViewBoard Screenshots 1 & 3).

---

## 🛠️ Floating Selection Context Action Bar Layout

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│  [🔒 ∨]  [◯]  [▦]  │  [<]  │  [🔲]  [📐 ∨]  │  [📑]  [📄]  [✂️]  [🥞 ∨]  [🎯]  [🗑️]  │  [...]  │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 📋 Context Bar Items & Engine Actions

| Icon | Action Name | Dropdown / Sub-Options | C++ Engine Function |
|------|-------------|------------------------|---------------------|
| `🔒 ∨` | **Lock Menu** | Lock Position, Lock Aspect Ratio, Lock Element | `SelectionManager::setLockState()` |
| `◯` | **Stroke Color** | Quick Color Swatches (Black, Red, Blue, Green, Yellow, Custom) | `SelectionManager::setStrokeColor()` |
| `▦` | **Fill & Pattern** | Solid Fill Colors, Translucent Fill, Grid Fill Patterns | `SelectionManager::setFillColor()` |
| `<` | **Expand / Collapse** | Toggles secondary toolbar items | UI local toggle |
| `🔲` | **Group / Ungroup** | Groups selected elements into one unit, or unwraps an existing group | `SelectionManager::groupSelected()` / `ungroupSelected()` |
| `📐 ∨` | **Align & Distribute** | Align Left, Center, Right, Top, Middle, Bottom, Distribute H/V | `SelectionManager::alignSelected()` |
| `📑` | **Duplicate** | Clones selected elements with a +20px offset | `SelectionManager::duplicateSelected()` |
| `📄` | **Copy** | Copies selected elements to system clipboard | `SelectionManager::copyToClipboard()` |
| `✂️` | **Cut** | Removes selected elements and copies to clipboard | `SelectionManager::cutToClipboard()` |
| `🥞 ∨` | **Layer Z-Order** | Bring to Front, Send to Back, Move Forward, Move Backward | `SelectionManager::bringToFront()`, etc. |
| `🎯` | **Zoom to Target** | Centers camera viewport smoothly on the selected bounds | `CanvasCamera::zoomToRect()` |
| `🗑️` | **Delete** | Removes selected elements (wrapped in `DeleteElementsCommand`) | `SelectionManager::deleteSelected()` |
| `...` | **Overflow Options** | Flip Horizontal, Flip Vertical, Export Selection (PNG / SVG) | `SelectionManager::flipHorizontal()`, etc. |
