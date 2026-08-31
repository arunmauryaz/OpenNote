#include "ob/CanvasCamera.h"
#include "ob/Document.h"
#include <algorithm>

namespace ob {

CanvasCamera::CanvasCamera()
    : m_zoom(1.0f), m_pan{0.0f, 0.0f},
      m_surfaceW(1920.0f), m_surfaceH(1080.0f),
      m_pageW(1920.0f), m_pageH(1080.0f)
{}

CanvasCamera::CanvasCamera(float pageW, float pageH, float surfaceW, float surfaceH) {
    resetView(pageW, pageH, surfaceW, surfaceH);
}

void CanvasCamera::resetView(float pageW, float pageH, float surfaceW, float surfaceH) {
    m_pageW    = pageW;
    m_pageH    = pageH;
    m_surfaceW = surfaceW;
    m_surfaceH = surfaceH;
    fitPageToScreen(pageW, pageH);
}

void CanvasCamera::fitPageToScreen(float pageW, float pageH) {
    m_pageW = pageW;
    m_pageH = pageH;
    if (pageW <= 0.f || pageH <= 0.f || m_surfaceW <= 0.f || m_surfaceH <= 0.f) return;

    float zoomX = m_surfaceW / pageW;
    float zoomY = m_surfaceH / pageH;
    m_zoom = std::min(zoomX, zoomY);
    m_zoom = std::clamp(m_zoom, MIN_ZOOM, MAX_ZOOM);

    float scaledW = pageW * m_zoom;
    float scaledH = pageH * m_zoom;
    m_pan.x = (m_surfaceW - scaledW) * 0.5f;
    m_pan.y = (m_surfaceH - scaledH) * 0.5f;
}

void CanvasCamera::setViewport(int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    m_surfaceW = (float)w;
    m_surfaceH = (float)h;
    onSurfaceChanged(m_surfaceW, m_surfaceH);
}

void CanvasCamera::fitToPage(const Page& page) {
    fitPageToScreen(page.width, page.height);
}

void CanvasCamera::panBy(float dx, float dy) {
    m_pan.x += dx;
    m_pan.y += dy;
}

void CanvasCamera::zoomBy(float factor, Vec2f focus) {
    zoomToLevel(m_zoom * factor, focus);
}

void CanvasCamera::zoomToLevel(float targetZoom, Vec2f focus) {
    float newZoom = std::clamp(targetZoom, MIN_ZOOM, MAX_ZOOM);
    if (std::abs(newZoom - m_zoom) < 0.0001f) return;

    Vec2f canvasFocus = screenToCanvas(focus);
    m_zoom = newZoom;
    Vec2f newScreenFocus = canvasToScreen(canvasFocus);
    m_pan.x += (focus.x - newScreenFocus.x);
    m_pan.y += (focus.y - newScreenFocus.y);
}

void CanvasCamera::onSurfaceChanged(float surfaceW, float surfaceH) {
    if (surfaceW <= 0.f || surfaceH <= 0.f) return;
    m_surfaceW = surfaceW;
    m_surfaceH = surfaceH;
}

Vec2f CanvasCamera::screenToCanvas(Vec2f p) const {
    return {(p.x - m_pan.x) / m_zoom, (p.y - m_pan.y) / m_zoom};
}

Rectf CanvasCamera::screenToCanvasRect(Rectf r) const {
    Vec2f tl = screenToCanvas({r.left,  r.top});
    Vec2f br = screenToCanvas({r.right, r.bottom});
    return {tl.x, tl.y, br.x, br.y};
}

Vec2f CanvasCamera::canvasToScreen(Vec2f p) const {
    return {p.x * m_zoom + m_pan.x, p.y * m_zoom + m_pan.y};
}

Rectf CanvasCamera::canvasToScreenRect(Rectf r) const {
    Vec2f tl = canvasToScreen({r.left,  r.top});
    Vec2f br = canvasToScreen({r.right, r.bottom});
    return {tl.x, tl.y, br.x, br.y};
}

Rectf CanvasCamera::viewportInCanvas() const {
    return screenToCanvasRect({0.0f, 0.0f, m_surfaceW, m_surfaceH});
}

} // namespace ob
