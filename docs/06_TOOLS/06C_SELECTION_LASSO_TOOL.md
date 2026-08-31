# 06C — Selection & Lasso Tool Engine

## Overview

The Selection & Lasso Tool Engine handles single-element and multi-element spatial queries, bounding box transformation handles (scale, stretch, rotate), layer z-order manipulation, alignment/distribution, and element grouping. 

Modeled after MyViewBoard's selection system, it supports both **Rectangular Selection** and **Freehand Lasso Selection** (orange dashed marquee).

---

## 🎯 Selection Modes & Spatial Algorithms

```
                          ┌────────────────────────────────┐
                          │   Selection Spatial Engine     │
                          └───────────────┬────────────────┘
                                          │
                  ┌───────────────────────┴───────────────────────┐
                  ▼                                               ▼
   ┌──────────────────────────────┐                ┌──────────────────────────────┐
   │ Rectangular Marquee Select   │                │ Freehand Lasso Select        │
   │ - Drag corner-to-corner box  │                │ - Draw orange dashed loop    │
   │ - Spatial R-Tree query       │                │ - Ray-casting Winding Number │
   └──────────────────────────────┘                └──────────────────────────────┘
```

### 1. Freehand Lasso Polygon Query (Winding Number Algorithm)
When the user draws an orange dashed lasso loop around canvas objects:
1. Touch points form a closed polygon \(P = \{p_0, p_1, \dots, p_n, p_0\}\).
2. The engine queries the layer R-Tree for candidate elements within the polygon's axis-aligned bounding box.
3. For each candidate element, the engine evaluates whether its control points fall inside \(P\) using the **Winding Number Algorithm**:

$$W(q, P) = \frac{1}{2\pi} \sum_{i=0}^{n-1} \theta_i \quad \text{where } \theta_i = \angle(p_i - q, p_{i+1} - q)$$

If \(W(q, P) \neq 0\), the point \(q\) is inside the lasso. Elements with at least 50% of control points inside the lasso are added to the active `SelectionGroup`.

---

## 📐 Transform Handles & Matrix Manipulation

Selected elements display an interactive yellow bounding box with **9 Transform Handles**:

```
      (Scale NW) ◯──────────────◯ (Stretch N) ──────────────◯ (Scale NE)
                 │                                          │
   (Stretch W) ◯ │              Selected Object             │ ◯ (Stretch E)
                 │                                          │
      (Scale SW) ◯──────────────◯ (Stretch S) ──────────────◯ (Scale SE)
                 🔄 (Rotate Handle - Bottom Left)
```

### Handle Actions

| Handle | Visual Position | Interaction Behavior |
|--------|-----------------|----------------------|
| **4 Corner Handles** | NW, NE, SW, SE circles | **Proportional Uniform Scaling**: Scales width and height while preserving aspect ratio. Holding Shift enables freeform scaling. |
| **4 Edge Handles** | N, S, E, W circles | **Non-Uniform Stretch**: Stretches width only (E/W) or height only (N/S). |
| **Rotation Handle** | `🔄` Arc Arrow (Bottom-Left) | **Center Rotation**: Dragging rotates the selection around its geometric center point \((X_c, Y_c)\). Snaps to \(0^\circ, 45^\circ, 90^\circ, 180^\circ\). |

---

## ⚙️ C++ SelectionManager API

```cpp
// core/include/openboard/selection_manager.h

class SelectionManager {
private:
    std::vector<uint32_t> selectedElementIds_;
    SkRect                groupBounds_;
    float                 rotationAngle_ = 0.0f;
    bool                  isLocked_ = false;

public:
    // Selection Management
    void selectElement(uint32_t elementId, bool additive = false);
    void selectLasso(const std::vector<SkPoint>& lassoPolygon);
    void selectRect(const SkRect& rect);
    void clearSelection();

    // Transformations
    void translateSelection(float dx, float dy);
    void scaleSelection(float scaleX, float scaleY, const SkPoint& pivot);
    void rotateSelection(float degrees, const SkPoint& pivot);

    // Grouping & Alignments
    void groupSelected();
    void ungroupSelected();
    void alignSelected(AlignmentMode mode); // Left, Center, Right, Top, Middle, Bottom
    void distributeSelected(DistributionMode mode); // Horizontal, Vertical

    // Z-Order Stack Reordering
    void bringToFront();
    void sendToBack();
    void moveForward();
    void moveBackward();

    // Clipboard
    void duplicateSelected();
    void copyToClipboard();
    void cutToClipboard();
    void deleteSelected();
};
```
