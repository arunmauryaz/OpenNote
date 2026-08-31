#pragma once

#include "Types.h"
#include "Document.h"
#include "CanvasCamera.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>

namespace ob {

// ─── OpenGL ES 3.0 Render Pipeline ───────────────────────────────────────────
// Manages EGL context, shader programs, VBOs, and draw calls.
// Draws: canvas background, strokes, shapes, text glyphs, UI overlays.

class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    // ── EGL Lifecycle ──────────────────────────────────────────────────────
    bool init(void* nativeWindow, int32_t w, int32_t h);
    void onSurfaceChanged(int32_t w, int32_t h);
    void destroy();

    // ── Main Render Entry ─────────────────────────────────────────────────
    // Called every vsync on the GL thread
    void renderFrame(const Document& doc, const CanvasCamera& camera);

    // ── Stats ─────────────────────────────────────────────────────────────
    uint32_t lastFps()   const { return m_lastFps;   }
    float    lastCpuMs() const { return m_lastCpuMs; }

    // ── Texture Management ────────────────────────────────────────────────
    uint32_t uploadTexture(const uint8_t* pixels, int32_t w, int32_t h,
                           bool hasAlpha = true);
    void     deleteTexture(uint32_t handle);

    // ── Stroke Batch Renderer ─────────────────────────────────────────────
    // Called per page during renderFrame
    void renderStrokes(const Page& page, const CanvasCamera& camera);
    void renderShapes(const Page& page, const CanvasCamera& camera);
    void renderImages(const Page& page, const CanvasCamera& camera);

    // ── Overlay (UI) Renderer ─────────────────────────────────────────────
    // Selection box, lasso loop, minimap, laser trail
    void renderSelectionOverlay(const Rectf& bounds);
    void renderLassoOverlay(const float* xArr, const float* yArr, int count);
    void renderMinimap(const Document& doc, const CanvasCamera& camera);

private:
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLSurface m_surface = EGL_NO_SURFACE;
    EGLContext m_context = EGL_NO_CONTEXT;

    int32_t m_width  = 0;
    int32_t m_height = 0;
    bool    m_initialized = false;

    // Shader programs
    GLuint m_strokeProgram  = 0;
    GLuint m_texProgram     = 0;
    GLuint m_colorProgram   = 0;

    // VBOs
    GLuint m_vbo     = 0;
    GLuint m_vao     = 0;

    // Stats
    uint32_t m_lastFps    = 0;
    float    m_lastCpuMs  = 0.0f;
    uint32_t m_frameCount = 0;
    int64_t  m_lastSecTs  = 0;

    // Shader helpers
    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vert, GLuint frag);
    void   checkGLError(const char* tag);

    // Background
    void renderBackground(const Page& page, const CanvasCamera& camera);

    void drawStrokeGeometry(const Stroke& s, const CanvasCamera& camera);
};

} // namespace ob
