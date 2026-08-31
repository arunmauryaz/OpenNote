# 15D — Pages Manager Panel (List & Grid Big Mode)

## Overview

Modeled after Screenshots 2 & 5 (MyViewBoard Pages Manager), the Pages Manager UI allows viewing page thumbnail cards, multi-selecting pages, and drag-and-drop page shuffling.

---

## 📱 Viewing Modes

### Mode 1: List Sidebar Mode
A vertical floating drawer containing page card thumbnails:
- **Card Header**: Page index number (`1`, `2`, `3`) with multi-select checkbox (`○` / `✓`).
- **Card Context Menu**: `...` button for page actions (Duplicate, Rename, Clear, Export Page).
- **Quick Add Card**: `+` card at bottom of scroll list to append a page instantly.

### Mode 2: Grid Big Mode (Overview Dialog)
A full-screen responsive card grid (3x4 grid) providing a bird's-eye view of all document pages.

---

## 🔀 Drag-and-Drop Page Shuffling

- **Long-Press Gesture**: Pressing down on a page card thumbnail for > 300ms lifts the card into a floating drag state (hovering above the canvas).
- **Interactive Reordering**: Dragging over adjacent page cards dynamically shifts cards out of the way.
- **Drop Commitment**: Releasing the finger commits the new page sequence via C++ `Document::movePage(from, to)`.

---

## 🛠️ Pages Bottom Action Bar

Located at the bottom of the Pages Manager panel:

| Icon | Action | Engine Command |
|------|--------|----------------|
| 🗑️ | **Delete Selected Pages** | `DeleteSelectedPagesCommand` |
| 👁️ | **Hide / Unhide Pages** | `SetPageVisibilityCommand` |
| 📌 | **Pin Panel** | Keeps panel docked while drawing |
| ▦ | **Toggle View Mode** | Toggles between List Sidebar and Grid Big Mode |
| ✖️ | **Close Panel** | Dismisses Pages Manager panel |
