#pragma once

#include "ob/Types.h"
#include "ob/Elements.h"   // for Page forward ref — included directly for Stroke bounds

namespace ob {

struct Page; // forward decl

class CanvasCamera {
public:
    // Default constructor: 1920×1080 canvas in 1920×1080 viewport at zoom=1
    CanvasCamera();
    CanvasCamera(float pageW, float pageH, float surfaceW, float surfaceH);

    // ── Setup ──────────────────────────────────────────────────────────────
    void setViewport(int32_t w, int32_t h);   // Called on surface create/resize
    void fitToPage(const Page& page);          // Zoom+pan so page fills viewport
    void fitPageToScreen(float pageW, float pageH);
    void resetView(float pageW, float pageH, float surfaceW, float surfaceH);

    // ── Navigation ────────────────────────────────────────────────────────
    void panBy(float dx, float dy);
    void zoomBy(float factor, Vec2f screenFocus);
    void zoomToLevel(float targetZoom, Vec2f screenFocus);

    // ── Surface changes ───────────────────────────────────────────────────
    void onSurfaceChanged(float surfaceW, float surfaceH);

    // ── Coordinate transforms ─────────────────────────────────────────────
    Vec2f screenToCanvas(Vec2f screenPt) const;
    Rectf screenToCanvasRect(Rectf r)    const;
    Vec2f canvasToScreen(Vec2f canvasPt) const;
    Rectf canvasToScreenRect(Rectf r)    const;

    // Viewport extents in canvas-space (for culling)
    Rectf viewportInCanvas() const;

    // ── State ─────────────────────────────────────────────────────────────
    float zoom()     const { return m_zoom;    }
    Vec2f pan()      const { return m_pan;     }
    float surfaceW() const { return m_surfaceW; }
    float surfaceH() const { return m_surfaceH; }

private:
    float m_zoom     = 1.0f;
    Vec2f m_pan      = {0.0f, 0.0f};
    float m_surfaceW = 1920.0f;
    float m_surfaceH = 1080.0f;
    float m_pageW    = 1920.0f;
    float m_pageH    = 1080.0f;

    static constexpr float MIN_ZOOM = 0.05f;
    static constexpr float MAX_ZOOM = 20.0f;
};

} // namespace ob
