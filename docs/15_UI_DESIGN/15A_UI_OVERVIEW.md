# 15A — UI Design System & Precision Aesthetics

## Overview

OpenBoard features a **Clean, Serious, Minimalist UI Design System**. The visual design focuses purely on **smooth edge curves**, **refined typography**, and **structured spatial arrangement**—creating a quiet, professional environment optimized for teaching and smartboard interaction.

---

## 📐 Geometric Curves & Structural Layout

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                   MINIMAL PRECISION DESIGN SYSTEM                           │
├─────────────────────────┬───────────────────────────────────────────────────┤
│ Edge Corner Radii       │ 14px - 16px Smooth Continuous Squircle Curve      │
│ Border Stroke           │ 1px Subdued Outline (rgba(255, 255, 255, 0.12))   │
│ Surface Background      │ Dark Glass (rgba(28, 28, 30, 0.92))               │
│ Backlighting Blur       │ backdrop-filter: blur(25px);                      │
│ Primary Accent Color    │ Electric Blue (#0A84FF)                           │
│ Typography Stack        │ Inter / SF Pro / Outfit (High-Legibility Sans)    │
│ Container Padding       │ 16px - 20px Uniform Spacing                       │
└─────────────────────────┴───────────────────────────────────────────────────┘
```

---

## 🔤 Typography & Contrast Hierarchy

- **Title / Header**: 18px Semi-Bold (`#FFFFFF`) — Clear, prominent modal & panel titles.
- **Section Label**: 14px Medium (`rgba(255, 255, 255, 0.85)`) — Group headers separating tool controls.
- **Body / Option Text**: 13px Regular (`#FFFFFF` primary, `rgba(255, 255, 255, 0.65)` secondary) — Crisp label and description text.
- **Caption / Subtext**: 11px Regular (`rgba(255, 255, 255, 0.45)`) — Helper text.

---

## 🧩 Spatial Arrangement & Component Styling

1. **Edge Curves & Borders**: Every dialog, toolbar, and popup uses 14px–16px squircle corners surrounded by a 1px subtle border stroke. This creates a sharp, well-defined visual separation against the canvas without heavy drop shadows.
2. **Segmented Options**: Multi-option selectors use pill-shaped containers with a highlighted active pill segment.
3. **Capsule Toggles**: Tactile capsule switches with 150ms smooth spring animation.
4. **Clean Grouping**: Settings and tool options are organized into structured vertical cards with generous spacing.

---

## 📐 Overall Floating Docks & Canvas Layout

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                        │
│                                  FULLSCREEN CANVAS                                     │
│                                                                                        │
│                                                           ┌─────────────────────────┐  │
│                                                           │ Minimap Overview Window │  │
│                                                           │ [🗺️ All Canvas Content] │  │
│                                                           │ [ [Viewport Rect]     ] │  │
│                                                           └─────────────────────────┘  │
│                                                           ┌─────────────────────────┐  │
│                                                           │ View & Zoom Dock        │  │
│                                                           │ > [🗺️] [🔍] [- 100% +]👤 │  │
│                                                           └─────────────────────────┘  │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ ┌────────────────┐ ┌──────────────────────────────────────┐ ┌────────┐                  │
│ │  Page Nav Dock │ │           Main Tool Dock             │ │ Undo/  │                  │
│ │ [🖼️] [< 2/12 >+] │ │ [↖️][✋][✏️][🧹][📐][T][📝][📁][🧰][🌐][📷][🎛️]│ │ Redo   │                  │
│ └────────────────┘ └──────────────────────────────────────┘ └────────┘                  │
└────────────────────────────────────────────────────────────────────────────────────────┘
```
