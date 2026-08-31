#pragma once

// ─── IRenderPipeline — Pure Virtual Renderer Interface ────────────────────────
//
// The engine core depends ONLY on this interface — never on EGL, GLES, Metal,
// Vulkan, or any other platform API. The concrete implementation lives in a
// platform-specific subclass (Android: GlesRenderPipeline, Windows: GlRenderPipeline).
//
// Dependency graph (no cycles):
//   IRenderPipeline  ←  Types.h, Document.h, CanvasCamera.h   (no platform headers)
//   GlesRenderPipeline → IRenderPipeline  +  EGL / GLES3      (Android only)
// ─────────────────────────────────────────────────────────────────────────────

#include "ob/Types.h"
#include "ob/Document.h"
#include "ob/CanvasCamera.h"
#include <cstdint>

namespace ob {

class IRenderPipeline {
public:
    virtual ~IRenderPipeline() = default;

    // ── Lifecycle ──────────────────────────────────────────────────────────
    // nativeWindowHandle: ANativeWindow* on Android, HWND on Windows, CAMetalLayer* on macOS
    virtual bool init(void* nativeWindowHandle, int32_t w, int32_t h) = 0;
    virtual void onSurfaceChanged(int32_t w, int32_t h)               = 0;
    virtual void destroy()                                             = 0;

    // ── Render ────────────────────────────────────────────────────────────
    // Called on the render thread once per vsync. Must be thread-safe with
    // respect to any mutations happening on the engine thread.
    virtual void renderFrame(const Document& doc, const CanvasCamera& camera) = 0;

    // ── Live Drawing ──────────────────────────────────────────────────────
    // The in-progress stroke before it is committed to the document.
    // Passed separately so the renderer can use front-buffer rendering.
    virtual void setLiveStroke(const Stroke* liveStroke) { (void)liveStroke; }
    virtual void setLiveShape(const ShapeElement* liveShape) { (void)liveShape; }
    virtual void setLassoPath(const std::vector<Vec2f>* lassoPath) { (void)lassoPath; }
    virtual void setSelectionBounds(const Rectf* bounds, float rotation = 0.0f) { (void)bounds; (void)rotation; }
    virtual void setLiveEraser(Vec2f screenPt, float screenRadius) { (void)screenPt; (void)screenRadius; }
    virtual void clearLiveEraser() {}

    // ── Texture Management ────────────────────────────────────────────────
    // Returns an opaque handle the engine can store in ImageElement.
    // Implementation allocates GPU texture resources.
    virtual uint32_t uploadTexture(const uint8_t* pixels, int32_t w, int32_t h,
                                   bool hasAlpha = true) = 0;
    virtual void     deleteTexture(uint32_t handle)      = 0;

    // ── Stats (optional — implementations may return 0) ───────────────────
    virtual uint32_t lastFps()   const { return 0; }
    virtual float    lastCpuMs() const { return 0.0f; }

    // ── Feature Flags ─────────────────────────────────────────────────────
    // Queried once after init(). Renderer self-reports what it supports.
    virtual bool supportsMSAA()         const { return false; }
    virtual bool supportsPartialUpdate() const { return false; }
    virtual bool isReady()              const { return false; }
};

} // namespace ob
