# 06G — Laser Pointer Tool

> **Module**: `tools/laser/`  
> **Primary class**: `LaserPointerTool`  
> **Dependencies**: Skia (`SkCanvas`, `SkPaint`, `SkPath`), RenderEngine (overlay layer), ViewportManager

---

## Table of Contents

1. [Purpose and Design Philosophy](#1-purpose-and-design-philosophy)
2. [LaserPointerTool Overview](#2-laserpointertool-overview)
3. [Trail Implementation — Ring Buffer](#3-trail-implementation--ring-buffer)
4. [Fade Animation](#4-fade-animation)
5. [Color Options](#5-color-options)
6. [Size Configuration](#6-size-configuration)
7. [Rendering — Above All Layers](#7-rendering--above-all-layers)
8. [Spotlight Mode](#8-spotlight-mode)
9. [Presentation Mode Integration](#9-presentation-mode-integration)
10. [LaserPointerTool API Reference](#10-laserpointertool-api-reference)

---

## 1. Purpose and Design Philosophy

The laser pointer is a **purely ephemeral** presentation aid. It produces no persistent content on the canvas:

- No strokes are created.
- No elements are modified.
- Nothing is pushed to the undo stack.
- The tool's visual output exists only in the render overlay and disappears automatically.

It is designed for use during presentations or collaborative sessions where the presenter needs to draw attention to specific areas of the whiteboard without leaving permanent marks.

### 1.1 Comparison With Drawing Tools

| Property | PenTool | LaserPointerTool |
|---|---|---|
| Persists after stylus lift | Yes | No |
| Stored in layer | Yes | Never |
| Appears in undo history | Yes | Never |
| Renders on committed layer | Yes | No (overlay only) |
| Active during presentation mode | Yes (if permitted) | Yes (primary tool) |
| Fades over time | No | Yes |

---

## 2. LaserPointerTool Overview

### 2.1 Behavioral Summary

1. When the user touches the screen, a glowing dot appears at the pointer position.
2. As the pointer moves, a trail of recent positions is drawn behind the dot, fading from bright at the head to transparent at the tail.
3. When the pointer is lifted, the trail continues to fade out over a configurable duration (default: 0.8 seconds).
4. No data is written to the canvas layer at any point.

### 2.2 Data Structures

```cpp
// tools/laser/LaserPointerTool.h
struct LaserPoint {
    float   x, y;           // canvas coordinates
    int64_t timestamp;       // milliseconds since epoch
    float   alpha;           // current alpha [0.0, 1.0]; decays over time
};
```

The trail is stored as a **ring buffer** of `LaserPoint` objects, allowing constant-time insertion at the head and automatic wrap-around at the tail (oldest points are overwritten):

```cpp
constexpr int RING_BUFFER_CAPACITY = 256;

struct LaserTrail {
    LaserPoint buffer[RING_BUFFER_CAPACITY];
    int        head  = 0;     // index of the most recent point
    int        count = 0;     // number of valid points (0..RING_BUFFER_CAPACITY)

    void push(LaserPoint p) {
        buffer[head] = p;
        head         = (head + 1) % RING_BUFFER_CAPACITY;
        if (count < RING_BUFFER_CAPACITY) ++count;
    }

    // Iterate from oldest to newest
    template<typename Fn>
    void forEach(Fn fn) const {
        int start = (count < RING_BUFFER_CAPACITY)
                    ? 0
                    : head;  // oldest is at head when buffer is full
        for (int i = 0; i < count; ++i) {
            fn(buffer[(start + i) % RING_BUFFER_CAPACITY], i, count);
        }
    }
};
```

---

## 3. Trail Implementation — Ring Buffer

### 3.1 Why a Ring Buffer?

The ring buffer provides:

- **O(1) insertion** of new points (no shifting of existing elements).
- **O(1) eviction** of oldest points (head simply advances).
- **Fixed memory footprint** regardless of how long the laser moves.
- **Cache-friendly access** (contiguous array).

### 3.2 Adding Points

Points are added to the trail on each `onPointerMove` event:

```cpp
void LaserPointerTool::onPointerMove(const PointerEvent& e) {
    LaserPoint pt;
    pt.x         = e.x;
    pt.y         = e.y;
    pt.timestamp = currentTimeMs();
    pt.alpha     = 1.0f;   // starts fully opaque

    m_trail.push(pt);
    m_lastX      = e.x;
    m_lastY      = e.y;
    m_lastMoveMs = pt.timestamp;

    // Invalidate only the region around the new segment
    SkRect dirty = SkRect::MakeLTRB(
        std::min(m_prevX, e.x) - m_radius * 2.0f,
        std::min(m_prevY, e.y) - m_radius * 2.0f,
        std::max(m_prevX, e.x) + m_radius * 2.0f,
        std::max(m_prevY, e.y) + m_radius * 2.0f);
    m_renderEngine->invalidate(dirty);
    m_prevX = e.x;
    m_prevY = e.y;
}
```

### 3.3 Point Decimation

To avoid redundant points at high sample rates, a minimum distance threshold `MIN_POINT_SPACING` (default: `3.0f` canvas pixels) is enforced:

```cpp
void LaserPointerTool::onPointerMove(const PointerEvent& e) {
    float dx = e.x - m_prevX, dy = e.y - m_prevY;
    if (dx*dx + dy*dy < MIN_POINT_SPACING * MIN_POINT_SPACING) return;
    // proceed to add point
}
```

---

## 4. Fade Animation

### 4.1 Decay Function

Each point's `alpha` decays linearly from `1.0` to `0.0` over `TRAIL_DURATION_MS` milliseconds:

```
alpha(t) = 1.0 - (now - point.timestamp) / TRAIL_DURATION_MS
```

Points with `alpha <= 0` are considered expired.

### 4.2 Per-Frame Update

The fade is driven by the render loop's `onAnimationTick` callback:

```cpp
void LaserPointerTool::onAnimationTick(float dtSeconds) {
    int64_t now      = currentTimeMs();
    bool    anyAlive = false;

    m_trail.forEach([&](LaserPoint& pt, int idx, int total) {
        float age  = (now - pt.timestamp) / 1000.0f;   // seconds
        pt.alpha   = std::max(0.0f, 1.0f - age / m_trailDurationSec);
        if (pt.alpha > 0.0f) anyAlive = true;
    });

    // Also fade the "head dot" after pointer is lifted
    if (m_pointerLifted) {
        float liftAge = (now - m_liftTimeMs) / 1000.0f;
        m_headAlpha   = std::max(0.0f, 1.0f - liftAge / m_fadeOutSec);
    }

    if (anyAlive || m_headAlpha > 0.0f) {
        // Schedule repaint of the trail bounding rect
        m_renderEngine->invalidate(computeTrailBounds());
    }
}
```

### 4.3 Trail Duration

| Setting | Default | Range |
|---|---|---|
| Trail duration | 0.8 sec | 0.0 – 3.0 sec |
| Fade-out after lift | 0.5 sec | 0.0 – 2.0 sec |

Setting trail duration to `0.0` produces a dot with no trail (just the cursor). Setting it to `3.0` produces a long, slowly fading trail useful for drawing attention to a path of motion.

### 4.4 Position-Based Alpha (Spatial Fade)

In addition to time-based fade, points can also fade based on their distance from the head (position-based), for a more visually continuous gradient:

```
spatialAlpha(idx, total) = idx / total   // 0 = oldest = transparent, 1 = newest = opaque
```

The final alpha used is `min(timeFade, spatialFade)` so both criteria must be satisfied for a point to be visible.

---

## 5. Color Options

### 5.1 Available Colors

The laser pointer color is selectable from:

| Color | RGBA | Notes |
|---|---|---|
| Red (default) | (1.0, 0.1, 0.1, 1.0) | High visibility on most backgrounds |
| Green | (0.1, 1.0, 0.1, 1.0) | Good contrast on dark backgrounds |
| Blue | (0.1, 0.3, 1.0, 1.0) | Calm, professional |
| Yellow | (1.0, 0.95, 0.0, 1.0) | High visibility on dark backgrounds |
| White | (1.0, 1.0, 1.0, 1.0) | Best on dark/colored backgrounds |
| Cyan | (0.0, 1.0, 1.0, 1.0) | Alternative to green |

```cpp
enum class LaserColor {
    Red, Green, Blue, Yellow, White, Cyan
};

SkColor4f LaserPointerTool::colorToSKColor(LaserColor c) {
    switch (c) {
        case LaserColor::Red:    return {1.0f, 0.1f, 0.1f, 1.0f};
        case LaserColor::Green:  return {0.1f, 1.0f, 0.1f, 1.0f};
        case LaserColor::Blue:   return {0.1f, 0.3f, 1.0f, 1.0f};
        case LaserColor::Yellow: return {1.0f, 0.95f, 0.0f, 1.0f};
        case LaserColor::White:  return {1.0f, 1.0f, 1.0f, 1.0f};
        case LaserColor::Cyan:   return {0.0f, 1.0f, 1.0f, 1.0f};
        default:                 return {1.0f, 0.1f, 0.1f, 1.0f};
    }
}
```

### 5.2 Glow Effect

The laser pointer dot has a multi-layer glow effect to enhance visibility against varied backgrounds:

- **Core**: Solid circle, full alpha.
- **Inner glow**: Circle at 1.5× radius, radial gradient from 70% alpha to 0%.
- **Outer glow**: Circle at 3× radius, radial gradient from 30% alpha to 0%.

```cpp
void LaserPointerTool::renderHead(SkCanvas* canvas, float x, float y, float alpha) {
    SkColor4f col = colorToSKColor(m_color);

    // Outer glow
    auto outerGrad = SkGradientShader::MakeRadial({x, y}, m_radius * 3.0f,
        (SkColor4f[]){ {col.fR, col.fG, col.fB, 0.3f * alpha},
                        {col.fR, col.fG, col.fB, 0.0f} },
        nullptr, 2, SkTileMode::kClamp);
    SkPaint outerPaint;
    outerPaint.setShader(outerGrad);
    canvas->drawCircle(x, y, m_radius * 3.0f, outerPaint);

    // Inner glow
    auto innerGrad = SkGradientShader::MakeRadial({x, y}, m_radius * 1.5f,
        (SkColor4f[]){ {col.fR, col.fG, col.fB, 0.7f * alpha},
                        {col.fR, col.fG, col.fB, 0.0f} },
        nullptr, 2, SkTileMode::kClamp);
    SkPaint innerPaint;
    innerPaint.setShader(innerGrad);
    canvas->drawCircle(x, y, m_radius * 1.5f, innerPaint);

    // Core dot
    SkPaint corePaint;
    corePaint.setColor4f({col.fR, col.fG, col.fB, alpha});
    canvas->drawCircle(x, y, m_radius, corePaint);
}
```

---

## 6. Size Configuration

### 6.1 Radius Range

The laser pointer radius is configurable from **2 px to 30 px** (canvas pixels):

```cpp
void LaserPointerTool::setRadius(float r) {
    m_radius = std::clamp(r, 2.0f, 30.0f);
}
```

### 6.2 Scale with Zoom

The radius is specified in **screen pixels**, not canvas pixels, so the laser pointer always appears the same physical size on screen regardless of canvas zoom:

```cpp
float LaserPointerTool::canvasRadius() const {
    return m_radius / m_viewport->zoom();
}
```

This means at 2× zoom, the canvas-space radius is half the screen radius, so the dot looks the same size on screen as at 1× zoom.

### 6.3 Trail Width

The trail is drawn with varying stroke width that tapers from full `m_radius` at the head to zero at the tail:

```cpp
void LaserPointerTool::renderTrail(SkCanvas* canvas) {
    float canvasR = canvasRadius();
    SkColor4f col = colorToSKColor(m_color);

    m_trail.forEach([&](const LaserPoint& pt, int idx, int total) {
        float t = (float)idx / total;   // 0.0 = oldest, 1.0 = newest
        float r = canvasR * t;          // tapered radius
        if (r < 0.5f || pt.alpha <= 0.0f) return;

        SkPaint p;
        p.setColor4f({col.fR, col.fG, col.fB, pt.alpha * t});
        canvas->drawCircle(pt.x, pt.y, r, p);
    });
}
```

A single `SkPath` with varying stroke width via a series of `drawCircle` calls at each trail point, or alternatively a variable-width filled path (similar to `StrokePathBuilder`) for smooth appearance.

---

## 7. Rendering — Above All Layers

### 7.1 Overlay Render Order

The render pipeline composites layers in this order:

```
1. Background layer
2. Content layers (layer 1, 2, 3, ...)
3. Selection overlay
4. Tool cursor overlay
5. **Laser pointer overlay**    <-- rendered LAST, always on top
6. Screen compositor (final output to display)
```

The laser pointer is drawn in `renderOverlay()`, which is called after all layer content has been rendered. This ensures the laser is always visible regardless of the content on the canvas.

### 7.2 No Layer Interaction

The laser pointer does **not** call `canvas->save()` / `canvas->restore()` around layer state — it draws directly to the final composite canvas surface. This means it cannot be obscured by any layer content.

```cpp
void RenderEngine::renderFrame(SkCanvas* screen) {
    // Phase 1: all layers (into offscreen composite)
    for (const Layer& layer : m_layerManager->visibleLayers()) {
        layer.render(m_compositeCanvas, m_viewport->zoom());
    }

    // Phase 2: blit composite to screen
    screen->drawImage(m_compositeImage, 0, 0);

    // Phase 3: overlays (selection, tool UI)
    m_activeTool->renderOverlay(screen);

    // Phase 4: laser pointer (always topmost)
    if (m_laserTool->isActive()) {
        m_laserTool->renderOverlay(screen);
    }
}
```

### 7.3 Anti-Aliasing and Blend Mode

The laser overlay uses `SkBlendMode::kSrcOver` with fully anti-aliased circles, ensuring the glow composites cleanly over any background color:

```cpp
corePaint.setAntiAlias(true);
corePaint.setBlendMode(SkBlendMode::kSrcOver);
```

---

## 8. Spotlight Mode

### 8.1 Concept

Spotlight mode dims the entire canvas except a circular region around the laser pointer, focusing audience attention:

```
+----------------------------------------+
|                                        |
|   [dim overlay covers everything]      |
|                                        |
|          O  <- bright circle           |
|         /|\  <- laser position         |
|          |                             |
|                                        |
+----------------------------------------+
```

### 8.2 Implementation

Spotlight is rendered as a full-screen dim rectangle with a hole punched at the laser position using `SkBlendMode::kDstOut`:

```cpp
void LaserPointerTool::renderSpotlight(SkCanvas* canvas) {
    if (!m_spotlightEnabled) return;

    float cx = m_lastX;
    float cy = m_lastY;
    float r  = m_spotlightRadius / m_viewport->zoom();

    // Step 1: Save layer for compositing
    SkPaint layerPaint;
    canvas->saveLayer(nullptr, &layerPaint);

    // Step 2: Draw full-screen dark overlay
    SkPaint dim;
    dim.setColor(SkColorSetARGB((uint8_t)(m_dimAlpha * 255), 0, 0, 0));
    canvas->drawRect(m_viewport->canvasBounds(), dim);

    // Step 3: Punch hole at laser position (DstOut erases overlay in this region)
    SkPaint hole;
    hole.setBlendMode(SkBlendMode::kDstOut);
    // Radial gradient: fully opaque at center (full punch) to transparent at edge (soft edge)
    auto grad = SkGradientShader::MakeRadial(
        {cx, cy}, r,
        (SkColor4f[]){{0,0,0,1.0f}, {0,0,0,0.8f}, {0,0,0,0.0f}},
        (float[]){0.0f, 0.6f, 1.0f}, 3, SkTileMode::kClamp);
    hole.setShader(grad);
    canvas->drawCircle(cx, cy, r, hole);

    canvas->restore();  // composite spotlight layer
}
```

### 8.3 Spotlight Settings

| Parameter | Default | Range |
|---|---|---|
| Enabled | Off | Toggle |
| Spotlight radius | 150 px (screen) | 50–400 px |
| Dim alpha | 0.7 (70% dark) | 0.3–0.9 |
| Spotlight edge softness | 0.4 | 0.0–1.0 |

### 8.4 Spotlight Follows Laser

The spotlight center updates in real-time with the laser pointer position. On `onPointerMove`, both `m_lastX`/`m_lastY` (used for trail) and the spotlight center are updated simultaneously.

---

## 9. Presentation Mode Integration

### 9.1 What Is Presentation Mode?

Presentation mode is a display configuration optimized for presenting the whiteboard to an audience:

- All toolbars and panels are hidden.
- The canvas takes up the full screen.
- Only essential controls remain visible (page navigation, laser pointer, exit button).
- Scroll and zoom may be locked to prevent accidental navigation.

### 9.2 Activation

Presentation mode can be activated via:
- A dedicated "Present" button in the toolbar.
- A keyboard shortcut (F5 or Ctrl+Shift+P).
- Connecting a secondary display (automatic activation when projector is detected).

```cpp
void PresentationManager::enterPresentationMode() {
    // Hide all tool panels
    m_mainWindow->hideAllPanels();

    // Show only the presentation HUD
    m_presentationHUD->show();
    m_presentationHUD->setActiveTool(m_laserTool);

    // Activate the laser pointer tool
    m_toolManager->setActiveTool(ToolType::LaserPointer);
    m_laserTool->onActivate();

    // Optionally lock viewport
    if (m_lockViewport) {
        m_viewport->setLocked(true);
    }

    // Emit signal for any connected displays
    emit presentationModeEntered();
}
```

### 9.3 Presentation HUD

The Presentation HUD (`PresentationHUD`) is a minimal overlay containing:

- **Page navigation**: Previous / Next page arrows.
- **Laser color picker**: Quick color change without opening a panel.
- **Spotlight toggle**: On/Off button.
- **Timer**: Optional countdown timer visible to the presenter.
- **Exit button**: Returns to normal editing mode.

```cpp
// ui/PresentationHUD.h
class PresentationHUD : public QWidget {
    Q_OBJECT
public:
    explicit PresentationHUD(QWidget* parent = nullptr);

    void setActiveTool(LaserPointerTool* tool);

signals:
    void pageNext();
    void pagePrev();
    void laserColorChanged(LaserColor);
    void spotlightToggled(bool);
    void exitPresentation();

private:
    QPushButton*    m_prevPage;
    QPushButton*    m_nextPage;
    QToolButton*    m_colorPicker;
    QToolButton*    m_spotlightBtn;
    QLabel*         m_timerLabel;
    QPushButton*    m_exitBtn;
    LaserPointerTool* m_laser;
};
```

### 9.4 Multi-Display Support

When presenting on a projector or secondary display, OpenBoard can show the canvas in a separate window on the external display while showing presenter notes and controls on the laptop screen:

```cpp
void PresentationManager::setupMultiDisplay() {
    QList<QScreen*> screens = QApplication::screens();
    if (screens.size() < 2) return;

    QScreen* primary    = screens[0];   // presenter screen
    QScreen* projector  = screens[1];   // audience screen

    m_presenterView->show();
    m_presenterView->windowHandle()->setScreen(primary);

    m_audienceView = new CanvasView(m_canvasScene);
    m_audienceView->setScreen(projector);
    m_audienceView->showFullScreen();
    m_audienceView->setViewOnly(true);   // no editing from audience view
}
```

The laser pointer renders on BOTH screens simultaneously, so the audience sees the laser in real-time.

---

## 10. LaserPointerTool API Reference

### 10.1 Full Class Declaration

```cpp
// tools/laser/LaserPointerTool.h
#pragma once
#include "BaseTool.h"

class LaserPointerTool : public BaseTool {
    Q_OBJECT
public:
    explicit LaserPointerTool(ViewportManager*, RenderEngine*,
                               QObject* parent = nullptr);
    ~LaserPointerTool() override = default;

    // BaseTool interface
    void onActivate()                       override;
    void onDeactivate()                     override;
    void onPointerDown(const PointerEvent&) override;
    void onPointerMove(const PointerEvent&) override;
    void onPointerUp  (const PointerEvent&) override;
    void onPointerCancel()                  override;
    void renderOverlay(SkCanvas*)           override;

    // Configuration
    void setColor           (LaserColor c);
    void setRadius          (float screenPx);        // [2.0, 30.0]
    void setTrailDuration   (float seconds);         // [0.0, 3.0]
    void setFadeOutDuration (float seconds);         // [0.0, 2.0]
    void setSpotlightEnabled(bool enabled);
    void setSpotlightRadius (float screenPx);        // [50.0, 400.0]
    void setDimAlpha        (float alpha);           // [0.3, 0.9]

    LaserColor  color()          const { return m_color; }
    float       radius()         const { return m_radius; }
    bool        spotlightEnabled()const { return m_spotlightEnabled; }
    bool        isPointerDown()  const { return m_pointerDown; }
    bool        hasActiveTrail() const;

    // Called by render loop every frame
    void onAnimationTick(float dtSeconds);

signals:
    void colorChanged      (LaserColor newColor);
    void radiusChanged     (float newRadius);
    void spotlightToggled  (bool enabled);
    void trailExpired      ();   // fired when all trail points fade out

private:
    void renderHead     (SkCanvas*, float x, float y, float alpha);
    void renderTrail    (SkCanvas*);
    void renderSpotlight(SkCanvas*);
    SkRect computeTrailBounds() const;
    float  canvasRadius() const;

    ViewportManager* m_viewport;
    RenderEngine*    m_renderEngine;

    LaserColor  m_color            = LaserColor::Red;
    float       m_radius           = 8.0f;         // screen pixels
    float       m_trailDurationSec = 0.8f;
    float       m_fadeOutSec       = 0.5f;

    // Spotlight
    bool        m_spotlightEnabled = false;
    float       m_spotlightRadius  = 150.0f;       // screen pixels
    float       m_dimAlpha         = 0.7f;

    // Trail state
    LaserTrail  m_trail;
    float       m_prevX = 0.0f;
    float       m_prevY = 0.0f;
    float       m_lastX = 0.0f;
    float       m_lastY = 0.0f;

    // Pointer state
    bool        m_pointerDown  = false;
    bool        m_pointerLifted = false;
    int64_t     m_liftTimeMs   = 0;
    float       m_headAlpha    = 0.0f;
};
```

### 10.2 Render Pipeline (Per Frame)

```
onAnimationTick(dt)
    |
    +-- For each trail point:
    |       alpha = 1.0 - (now - timestamp) / trailDuration
    |
    +-- If pointer lifted:
    |       headAlpha = 1.0 - (now - liftTime) / fadeOutDuration
    |
    +-- If any point alive OR headAlpha > 0:
    |       invalidate(computeTrailBounds())
    |
    v
renderOverlay(canvas)
    |
    +-- renderSpotlight(canvas)   [if spotlightEnabled]
    |       -> full-screen dim + DstOut circle
    |
    +-- renderTrail(canvas)
    |       -> for each trail point: draw circle(pt.x, pt.y, radius*t, alpha)
    |
    +-- renderHead(canvas)        [if pointerDown OR headAlpha > 0]
            -> outer glow circle
            -> inner glow circle
            -> core circle
```

### 10.3 No Undo Stack Interactions

The laser pointer tool is intentionally isolated from the undo/redo system:

- `onPointerDown`, `onPointerMove`, `onPointerUp` never call `m_undoStack->push()`.
- The trail data in `m_trail` is volatile RAM only.
- Switching tools or closing the document does not preserve any laser trail data.
- There is no "laser history" or replay feature.

This is a deliberate design decision: the laser pointer is a live presentation aid, not a content creation tool. Keeping it out of the undo stack prevents accidental undo operations from affecting the laser (which would be confusing and meaningless).

### 10.4 Performance Characteristics

| Metric | Value |
|---|---|
| Trail ring buffer size | 256 points |
| Memory per trail point | 20 bytes (x, y, ts, alpha = 4+4+8+4) |
| Total trail memory | ~5 KB (constant, regardless of duration) |
| Draw calls per frame (trail) | 1 SkPath or up to 256 drawCircle calls |
| Dirty rect per frame | Trail bounding box + spotlight area |
| CPU time per frame | < 0.5 ms |

The laser pointer is designed to have negligible performance impact, even when running at 120 Hz on the render loop.

---

*End of 06G — Laser Pointer Tool*
