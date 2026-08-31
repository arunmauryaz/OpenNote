# 06A — Pen & Brush Engine

## Overview

The Pen & Brush Engine is the core drawing subsystem of OpenBoard. Modeled after MyViewBoard's rich pen tools flyout, OpenBoard supports multiple specialized pen types, real-time Bézier smoothing, pressure curves, thickness presets, and interactive shader effects.

---

## ✒️ Pen Tools Flyout Architecture (MyViewBoard Style)

```
┌────────────────────────────────────────────────────────┐
│ Pen Tools                                            X │
├────────────────────────────────────────────────────────┤
│ Pen Types                                              │
│  [✏️ Pen]  [🖌️ Brush]  [🖍️ Highlighter]  [💮 Stamp]   │
│  [🪄 Laser] [🤖 AI Shape] [✨ Pattern Ink]              │
├────────────────────────────────────────────────────────┤
│ Stroke Thickness Presets                               │
│  ( • Fine: 2px )  ( • Medium: 6px )  ( • Thick: 12px ) │
├────────────────────────────────────────────────────────┤
│ Quick Color Palette                                    │
│  [⚫ Black]  [🔴 Red]  [🔵 Blue]  [🎨 Custom Picker]   │
└────────────────────────────────────────────────────────┘
```

---

## 🎨 Supported Pen Types

| Icon | Pen Type Name | Rendering Shader / Algorithm | Description |
|------|---------------|------------------------------|-------------|
| ✏️ | **Standard Pen** | Solid SkPath / Catmull-Rom | Classic smooth ink with pressure sensitivity. |
| 🖌️ | **Paint Brush** | Bristle Texture Alpha Map | Dynamic width and bristle blending mimicking real paint. |
| 🖍️ | **Highlighter** | SkBlendMode::kSrcOver (50% Alpha) | Translucent ink designed for overlaying text without obscuring it. |
| 💮 | **Stamp Pen** | Sprite Array Stamp Dispatcher | Stamps repeating icon patterns (stars, checkmarks, emojis) along touch path. |
| 🪄 | **Laser / Magic Pen** | Ephemeral Ring Buffer + Blur | Glow effect trail that automatically fades away after 1-2 seconds. |
| 🤖 | **AI Shape Recognizer** | Real-time Geometry Classifier | Automatically converts hand-drawn rough strokes into perfect lines, circles, rectangles, or arrows on `TOUCH_UP`. |
| ✨ | **Pattern / Glitter Pen** | Procedural Particle Shader | Renders sparkling particle effects or textured ink patterns. |

---

## ⚙️ Pen Tool Data Model & Class API

```cpp
// core/include/openboard/pen_tool.h

enum class PenType {
    StandardPen,
    PaintBrush,
    Highlighter,
    StampPen,
    LaserMagicPen,
    AIShapeRecognizer,
    PatternGlitterPen
};

struct PenStyle {
    PenType  type = PenType::StandardPen;
    float    thickness = 6.0f;           // Stroke width in canvas units
    uint32_t colorARGB = 0xFF000000;     // Solid ARGB color
    float    opacity = 1.0f;             // 0.0f - 1.0f opacity
    SkBlendMode blendMode = SkBlendMode::kSrcOver;
    std::string stampSpriteId;           // Sprite ID for StampPen
};

class PenTool : public DrawingTool {
private:
    PenStyle style_;
    std::vector<OBPoint> rawPoints_;
    SkPath currentPath_;

public:
    void setPenType(PenType type);
    void setThickness(float thickness);
    void setColor(uint32_t colorARGB);
    void setOpacity(float opacity);

    void onPointerDown(const InputEvent& event) override;
    void onPointerMove(const InputEvent& event) override;
    void onPointerUp(const InputEvent& event) override;
    
    void renderLivePreview(SkCanvas* canvas) override;
};
```

---

## 📈 Pressure Normalization & Curve Fitting

### Schneider's Bézier Fitting
Raw touch points are converted into smooth cubic Bézier curves using Schneider's algorithm to eliminate jagged edges on touch displays:

$$\mathbf{B}(t) = (1-t)^3 \mathbf{P}_0 + 3(1-t)^2 t \mathbf{P}_1 + 3(1-t) t^2 \mathbf{P}_2 + t^3 \mathbf{P}_3, \quad t \in [0, 1]$$

### Pressure Width Mapping
Stylus pressure (\(P \in [0.0, 1.0]\)) maps dynamically to stroke width:

$$W_{effective} = W_{base} \cdot \left(0.2 + 0.8 \cdot P^2\right)$$
