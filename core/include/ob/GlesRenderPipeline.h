#pragma once

// ─── GlesRenderPipeline — Android OpenGL ES 3.0 implementation ───────────────
//
// This is the Android-specific concrete renderer. The engine core never
// includes this header — only the JNI bridge and Android platform layer do.
//
// To port to a new platform, create e.g. GlRenderPipeline (Windows OpenGL 4.5)
// or MetalRenderPipeline (macOS/iOS) and inherit from IRenderPipeline.
// ─────────────────────────────────────────────────────────────────────────────

#include "ob/IRenderPipeline.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>

namespace ob {

class GlesRenderPipeline final : public IRenderPipeline {
public:
    GlesRenderPipeline();
    ~GlesRenderPipeline() override;

    // IRenderPipeline overrides
    bool     init(void* nativeWindowHandle, int32_t w, int32_t h) override;
    void     onSurfaceChanged(int32_t w, int32_t h) override;
    void     destroy() override;
    void     renderFrame(const Document& doc, const CanvasCamera& camera) override;
    void     setLiveStroke(const Stroke* liveStroke) override;
    void     setLiveShape(const ShapeElement* liveShape) override { m_liveShape = liveShape; }
    void     setLassoPath(const std::vector<Vec2f>* lassoPath) override { m_lassoPath = lassoPath; }
    void     setSelectionBounds(const Rectf* bounds, float rotation = 0.0f) override {
        m_selectionBounds = bounds;
        m_selectionRotation = rotation;
    }
    void     setLiveEraser(Vec2f screenPt, float screenRadius) override {
        m_liveEraserPt = screenPt;
        m_liveEraserRadius = screenRadius;
    }
    void     clearLiveEraser() override { m_liveEraserRadius = 0.f; }
    uint32_t uploadTexture(const uint8_t* pixels, int32_t w, int32_t h,
                           bool hasAlpha = true) override;
    void     deleteTexture(uint32_t handle) override;
    uint32_t lastFps()            const override { return m_lastFps; }
    float    lastCpuMs()          const override { return m_lastCpuMs; }
    bool     supportsMSAA()       const override { return m_msaaEnabled; }
    bool     isReady()            const override { return m_initialized; }

    void renderLasso(const std::vector<Vec2f>& lassoPath, const CanvasCamera& camera);
    void renderSelectionBoundingBox(const Rectf& bounds, float rotation, bool is3D, const CanvasCamera& camera);
    void renderEraserCircle(Vec2f screenPt, float screenRadius);
    void renderShape(const ShapeElement& shape, const CanvasCamera& camera);

private:
    // ── EGL state ─────────────────────────────────────────────────────────
    EGLDisplay m_display    = EGL_NO_DISPLAY;
    EGLSurface m_surface    = EGL_NO_SURFACE;
    EGLContext m_context    = EGL_NO_CONTEXT;

    // ── Dimensions ────────────────────────────────────────────────────────
    int32_t m_width     = 0;
    int32_t m_height    = 0;
    bool    m_initialized = false;
    bool    m_msaaEnabled = false;

    // ── Shader programs ───────────────────────────────────────────────────
    GLuint m_strokeProgram = 0;   // Stroke with round-cap SDF
    GLuint m_colorProgram  = 0;   // Flat-color quads (background, UI)
    GLuint m_texProgram    = 0;   // Textured quads (images)

    // ── GPU buffers ───────────────────────────────────────────────────────
    GLuint m_vbo     = 0;   // General-purpose dynamic VBO (committed strokes)
    GLuint m_liveVbo = 0;   // Dedicated VBO for live in-progress stroke (tail-only updates)
    GLsizeiptr m_liveVboSize = 0; // Current allocated size of m_liveVbo in bytes

    // ── Cached shader uniform / attrib locations (set once at link time) ──
    // Stroke program
    GLint m_stroke_mvpLoc   = -1;
    GLint m_stroke_colorLoc = -1;
    GLint m_stroke_posLoc   = -1;
    GLint m_stroke_uvLoc    = -1;
    // Color program
    GLint m_color_mvpLoc    = -1;
    GLint m_color_colorLoc  = -1;
    GLint m_color_posLoc    = -1;
    // Texture program
    GLint m_tex_mvpLoc      = -1;
    GLint m_tex_texLoc      = -1;
    GLint m_tex_posLoc      = -1;
    GLint m_tex_uvLoc       = -1;

    // ── Live stroke, Live Shape, Lasso & Selection ───────────────────────
    const Stroke*             m_liveStroke        = nullptr;
    const ShapeElement*       m_liveShape         = nullptr;
    const std::vector<Vec2f>* m_lassoPath         = nullptr;
    const Rectf*              m_selectionBounds   = nullptr;
    float                     m_selectionRotation = 0.0f;
    Vec2f                     m_liveEraserPt{0.f, 0.f};
    float                     m_liveEraserRadius  = 0.f;

    // ── Stats ─────────────────────────────────────────────────────────────
    uint32_t m_lastFps    = 0;
    float    m_lastCpuMs  = 0.0f;
    uint32_t m_frameCount = 0;
    int64_t  m_lastSecTs  = 0;

    // ── Draw helpers ──────────────────────────────────────────────────────
    void renderBackground(const Page& page, const CanvasCamera& camera);
    void renderStrokes(const Page& page, const CanvasCamera& camera);
    void renderShapes(const Page& page, const CanvasCamera& camera);
    void renderImages(const Page& page, const CanvasCamera& camera);
    void drawStroke(const Stroke& s, const CanvasCamera& camera, bool isLiveStroke = false);
    void drawRect(const Rectf& screenRect, const Color& color);
    void buildOrthoMVP(float out[16]) const;

    // ── Shader helpers ────────────────────────────────────────────────────
    GLuint compileShader(GLenum type, const char* src);
    GLuint linkProgram(GLuint vert, GLuint frag);
    void   cacheShaderLocations();   // Called once after all programs are linked
    void   checkGLError(const char* tag);
};

} // namespace ob
