#include "ob/GlesRenderPipeline.h"
#include "ob/Document.h"
#include "ob/CanvasCamera.h"
#include <android/log.h>
#include <android/native_window.h>
#include <cstring>
#include <chrono>
#include <cmath>

#define LOG_TAG "OB_Render"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ob {

static const char* kStrokeVertSrc = R"(#version 300 es
in vec2 aPos;
in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char* kStrokeFragSrc = R"(#version 300 es
precision highp float;
in  vec2  vUV;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    // vUV.y == 0.0 means stroke body; vUV.y != 0.0 means end-cap disc
    // For stroke body:  dist = |vUV.x|,       edge at 1.0
    // For end-cap disc: dist = length(vUV),   edge at 1.0
    float dist = (abs(vUV.y) > 0.01) ? length(vUV) : abs(vUV.x);
    // Exact 1-pixel linear anti-aliased edge: completely solid interior (alpha=1.0),
    // crisp 1-pixel transition right at the boundary without any inner blur or haze.
    float fw = max(fwidth(dist), 0.0001);
    float alpha = clamp((1.0 - dist) / fw, 0.0, 1.0);
    if (alpha < 0.01) discard;
    fragColor = vec4(uColor.rgb, uColor.a * alpha);
}
)";

static const char* kColorVertSrc = R"(#version 300 es
in vec2 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

static const char* kColorFragSrc = R"(#version 300 es
precision mediump float;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

static const char* kTexVertSrc = R"(#version 300 es
in vec2 aPos;
in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char* kTexFragSrc = R"(#version 300 es
precision mediump float;
in  vec2      vUV;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV);
}
)";

static void buildOrthoMat(float out[16], float l, float r, float b, float t) {
    memset(out, 0, 64);
    out[0]  = 2.0f/(r-l);
    out[5]  = 2.0f/(t-b);
    out[10] = -1.0f;
    out[12] = -(r+l)/(r-l);
    out[13] = -(t+b)/(t-b);
    out[15] = 1.0f;
}

GlesRenderPipeline::GlesRenderPipeline() = default;
GlesRenderPipeline::~GlesRenderPipeline() { destroy(); }

bool GlesRenderPipeline::init(void* nativeWindow, int32_t w, int32_t h) {
    if (!nativeWindow || w <= 0 || h <= 0) {
        LOGE("Invalid params for RenderPipeline::init: window=%p w=%d h=%d", nativeWindow, w, h);
        return false;
    }
    m_width  = w;
    m_height = h;

    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }

    EGLint major, minor;
    if (!eglInitialize(m_display, &major, &minor)) {
        LOGE("eglInitialize failed"); return false;
    }

    const EGLint configAttribsMSAA[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      0,
        EGL_STENCIL_SIZE,    8,
        EGL_SAMPLE_BUFFERS,  1,
        EGL_SAMPLES,         4,
        EGL_NONE
    };

    const EGLint configAttribsStandard[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(m_display, configAttribsMSAA, &config, 1, &numConfigs) || numConfigs < 1) {
        LOGI("MSAA 4x config unavailable, falling back to standard EGL config");
        if (!eglChooseConfig(m_display, configAttribsStandard, &config, 1, &numConfigs) || numConfigs < 1) {
            LOGE("eglChooseConfig failed for standard EGL config");
            return false;
        }
    }

    m_surface = eglCreateWindowSurface(m_display, config,
                    (EGLNativeWindowType)nativeWindow, nullptr);
    if (m_surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed, EGL error: 0x%x", eglGetError());
        return false;
    }

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, ctxAttribs);
    if (m_context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed, EGL error: 0x%x", eglGetError());
        return false;
    }

    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        LOGE("eglMakeCurrent failed, EGL error: 0x%x", eglGetError());
        return false;
    }

    eglSwapInterval(m_display, 0);

    GLuint sv = compileShader(GL_VERTEX_SHADER,   kStrokeVertSrc);
    GLuint sf = compileShader(GL_FRAGMENT_SHADER, kStrokeFragSrc);
    m_strokeProgram = linkProgram(sv, sf);
    glDeleteShader(sv); glDeleteShader(sf);

    GLuint cv = compileShader(GL_VERTEX_SHADER,   kColorVertSrc);
    GLuint cf = compileShader(GL_FRAGMENT_SHADER, kColorFragSrc);
    m_colorProgram = linkProgram(cv, cf);
    glDeleteShader(cv); glDeleteShader(cf);

    GLuint tv = compileShader(GL_VERTEX_SHADER,   kTexVertSrc);
    GLuint tf = compileShader(GL_FRAGMENT_SHADER, kTexFragSrc);
    m_texProgram = linkProgram(tv, tf);
    glDeleteShader(tv); glDeleteShader(tf);

    if (m_strokeProgram == 0 || m_colorProgram == 0 || m_texProgram == 0) {
        LOGE("Failed to compile or link OpenGL ES 3.0 shader programs");
        return false;
    }

    // Cache all uniform/attrib locations once — never call glGetUniformLocation per frame
    cacheShaderLocations();

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Dedicated live-stroke VBO — pre-allocate 64 KB for the in-progress stroke.
    // Only the tail delta is uploaded each frame via glBufferSubData.
    glGenBuffers(1, &m_liveVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_liveVbo);
    m_liveVboSize = 65536;
    glBufferData(GL_ARRAY_BUFFER, m_liveVboSize, nullptr, GL_STREAM_DRAW);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_initialized = true;
    LOGI("RenderPipeline: OpenGL ES 3.0 ready, %dx%d", w, h);
    return true;
}

void GlesRenderPipeline::onSurfaceChanged(int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    m_width  = w;
    m_height = h;
    glViewport(0, 0, w, h);
}

void GlesRenderPipeline::destroy() {
    if (!m_initialized) return;
    if (m_strokeProgram) { glDeleteProgram(m_strokeProgram); m_strokeProgram = 0; }
    if (m_colorProgram)  { glDeleteProgram(m_colorProgram);  m_colorProgram  = 0; }
    if (m_texProgram)    { glDeleteProgram(m_texProgram);    m_texProgram    = 0; }
    if (m_vbo)           { glDeleteBuffers(1, &m_vbo);       m_vbo           = 0; }
    if (m_liveVbo)       { glDeleteBuffers(1, &m_liveVbo);   m_liveVbo       = 0; m_liveVboSize = 0; }
    if (m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_surface);
        if (m_context != EGL_NO_CONTEXT) eglDestroyContext(m_display, m_context);
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        m_surface = EGL_NO_SURFACE;
        m_context = EGL_NO_CONTEXT;
    }
    m_initialized = false;
    LOGI("RenderPipeline destroyed");
}

void GlesRenderPipeline::renderFrame(const Document& doc, const CanvasCamera& camera) {
    if (!m_initialized || m_colorProgram == 0) return;

    glViewport(0, 0, m_width, m_height);

    const Page* page = const_cast<Document&>(doc).activePage_ptr();
    Color bg = page ? page->background.color : Color::white();
    if (bg.a == 0 && bg.r == 0 && bg.g == 0 && bg.b == 0) {
        bg = Color::white();
    }
    glClearColor(bg.r / 255.f, bg.g / 255.f, bg.b / 255.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (page) {
        renderBackground(*page, camera);
        renderImages(*page, camera);
        renderShapes(*page, camera);
        if (m_liveShape && !m_liveShape->isErased) {
            renderShape(*m_liveShape, camera);
        }
        renderStrokes(*page, camera);
        // Also render live stroke (in-progress) over committed strokes
        if (m_liveStroke && !m_liveStroke->isErased && m_liveStroke->points.size() >= 2) {
            drawStroke(*m_liveStroke, camera, /*isLiveStroke=*/true);
        }
        if (m_lassoPath && m_lassoPath->size() >= 2) {
            renderLasso(*m_lassoPath, camera);
        }
        if (m_liveEraserRadius > 0.5f) {
            renderEraserCircle(m_liveEraserPt, m_liveEraserRadius);
        }

        // Selection overlay rendered LAST so it sits on top of all canvas objects
        if (m_selectionBounds) {
            renderSelectionBoundingBox(*m_selectionBounds, m_selectionRotation, camera);
        }
    }

    if (m_display != EGL_NO_DISPLAY && m_surface != EGL_NO_SURFACE) {
        eglSwapBuffers(m_display, m_surface);
    }
}

void GlesRenderPipeline::renderBackground(const Page& page, const CanvasCamera& camera) {
    if (m_colorProgram == 0) return;
    Rectf pageScreenRect = camera.canvasToScreenRect({0, 0, page.width, page.height});

    float mvp[16];
    buildOrthoMat(mvp, 0.f, (float)m_width, (float)m_height, 0.f);

    Color bg = page.background.color;
    if (bg.a == 0 && bg.r == 0 && bg.g == 0 && bg.b == 0) {
        bg = Color::white();
    }

    // 1. Draw Background: Texture if set, or Crisp Plain Color Paper Sheet
    if (page.background.textureId > 0 && m_texProgram != 0) {
        glUseProgram(m_texProgram);
        GLint tMvpLoc = glGetUniformLocation(m_texProgram, "uMVP");
        GLint tTexLoc = glGetUniformLocation(m_texProgram, "uTex");
        glUniformMatrix4fv(tMvpLoc, 1, GL_FALSE, mvp);
        glUniform1i(tTexLoc, 0);

        GLint tPosLoc = glGetAttribLocation(m_texProgram, "aPos");
        GLint tUvLoc  = glGetAttribLocation(m_texProgram, "aUV");
        glEnableVertexAttribArray(tPosLoc);
        glEnableVertexAttribArray(tUvLoc);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)page.background.textureId);

        float bgVerts[] = {
            pageScreenRect.left,  pageScreenRect.top,    0.0f, 0.0f,
            pageScreenRect.right, pageScreenRect.top,    1.0f, 0.0f,
            pageScreenRect.left,  pageScreenRect.bottom, 0.0f, 1.0f,
            pageScreenRect.right, pageScreenRect.bottom, 1.0f, 1.0f,
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(bgVerts), bgVerts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(tPosLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(tUvLoc,  2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(tUvLoc);
        glDisableVertexAttribArray(tPosLoc);
    } else {
        glUseProgram(m_colorProgram);
        GLint mvpLoc   = glGetUniformLocation(m_colorProgram, "uMVP");
        GLint colorLoc = glGetUniformLocation(m_colorProgram, "uColor");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
        GLint posLoc   = glGetAttribLocation(m_colorProgram, "aPos");
        glEnableVertexAttribArray(posLoc);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

        glUniform4f(colorLoc, bg.r/255.f, bg.g/255.f, bg.b/255.f, 1.0f);
        float paperVerts[] = {
            pageScreenRect.left,  pageScreenRect.top,
            pageScreenRect.right, pageScreenRect.top,
            pageScreenRect.left,  pageScreenRect.bottom,
            pageScreenRect.right, pageScreenRect.bottom,
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(paperVerts), paperVerts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(posLoc);
    }

    // 2. Draw Background Grid if active (1: Square Grid, 2: Ruled Lines, 3: Dot Grid)
    if (page.background.gridType > 0) {
        glUseProgram(m_colorProgram);
        GLint mvpLoc   = glGetUniformLocation(m_colorProgram, "uMVP");
        GLint colorLoc = glGetUniformLocation(m_colorProgram, "uColor");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
        GLint posLoc   = glGetAttribLocation(m_colorProgram, "aPos");
        glEnableVertexAttribArray(posLoc);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        float lum = 0.299f * (bg.r / 255.f) + 0.587f * (bg.g / 255.f) + 0.114f * (bg.b / 255.f);
        if (lum > 0.5f) {
            glUniform4f(colorLoc, 0.0f, 0.0f, 0.0f, 0.09f); // Subtle dark grid on light paper
        } else {
            glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 0.14f); // Subtle light grid on dark chalkboard
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        float spacing = 50.0f; // 50 canvas units spacing
        std::vector<float> lineVerts;

        if (page.background.gridType == 1) {
            // Square Grid
            for (float x = spacing; x < page.width; x += spacing) {
                Vec2f top = camera.canvasToScreen({x, 0});
                Vec2f bot = camera.canvasToScreen({x, page.height});
                lineVerts.push_back(top.x); lineVerts.push_back(top.y);
                lineVerts.push_back(bot.x); lineVerts.push_back(bot.y);
            }
            for (float y = spacing; y < page.height; y += spacing) {
                Vec2f l = camera.canvasToScreen({0, y});
                Vec2f r = camera.canvasToScreen({page.width, y});
                lineVerts.push_back(l.x); lineVerts.push_back(l.y);
                lineVerts.push_back(r.x); lineVerts.push_back(r.y);
            }
            if (!lineVerts.empty()) {
                glLineWidth(1.0f);
                glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(float), lineVerts.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
                glDrawArrays(GL_LINES, 0, (GLsizei)(lineVerts.size() / 2));
            }
        } else if (page.background.gridType == 2) {
            // Ruled notebook lines
            for (float y = spacing; y < page.height; y += spacing) {
                Vec2f l = camera.canvasToScreen({0, y});
                Vec2f r = camera.canvasToScreen({page.width, y});
                lineVerts.push_back(l.x); lineVerts.push_back(l.y);
                lineVerts.push_back(r.x); lineVerts.push_back(r.y);
            }
            if (!lineVerts.empty()) {
                glLineWidth(1.0f);
                glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(float), lineVerts.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
                glDrawArrays(GL_LINES, 0, (GLsizei)(lineVerts.size() / 2));
            }
        } else if (page.background.gridType == 3) {
            // Dot Grid
            std::vector<float> dotVerts;
            float dotR = 1.5f;
            for (float x = spacing; x < page.width; x += spacing) {
                for (float y = spacing; y < page.height; y += spacing) {
                    Vec2f p = camera.canvasToScreen({x, y});
                    dotVerts.push_back(p.x - dotR); dotVerts.push_back(p.y - dotR);
                    dotVerts.push_back(p.x + dotR); dotVerts.push_back(p.y - dotR);
                    dotVerts.push_back(p.x - dotR); dotVerts.push_back(p.y + dotR);

                    dotVerts.push_back(p.x + dotR); dotVerts.push_back(p.y - dotR);
                    dotVerts.push_back(p.x + dotR); dotVerts.push_back(p.y + dotR);
                    dotVerts.push_back(p.x - dotR); dotVerts.push_back(p.y + dotR);
                }
            }
            if (!dotVerts.empty()) {
                glBufferData(GL_ARRAY_BUFFER, dotVerts.size() * sizeof(float), dotVerts.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(dotVerts.size() / 2));
            }
        }
        glDisableVertexAttribArray(posLoc);
    }
}

void GlesRenderPipeline::renderStrokes(const Page& page, const CanvasCamera& camera) {
    if (page.strokes.empty() || m_strokeProgram == 0) return;
    Rectf viewport = camera.viewportInCanvas();
    for (const auto& [id, stroke] : page.strokes) {
        if (stroke.isErased || stroke.points.empty()) continue;
        if (!stroke.bounds.intersects(viewport)) continue;
        drawStroke(stroke, camera);
    }
}

void GlesRenderPipeline::drawStroke(const Stroke& s, const CanvasCamera& camera, bool isLiveStroke) {
    if (s.points.empty() || m_strokeProgram == 0) return;

    const auto& pts = s.points;
    size_t n = pts.size();

    float mvp[16];
    buildOrthoMat(mvp, 0.f, (float)m_width, (float)m_height, 0.f);

    glUseProgram(m_strokeProgram);
    // Use pre-cached locations — zero driver overhead per frame
    glUniformMatrix4fv(m_stroke_mvpLoc, 1, GL_FALSE, mvp);

    float baseAlpha = s.style.color.a / 255.f * s.style.opacity;
    if (s.style.penType == 2) baseAlpha *= 0.35f; // Highlighter semi-transparency

    glUniform4f(m_stroke_colorLoc,
                s.style.color.r / 255.f,
                s.style.color.g / 255.f,
                s.style.color.b / 255.f,
                baseAlpha);

    auto buildMesh = [&](float widthMult, std::vector<float>& stripVerts, std::vector<float>& discVerts) {
        if (n == 0) return;

        // Convert points to screen space and compute half widths
        std::vector<Vec2f> screenPts(n);
        std::vector<float> halfWs(n);
        float zoom = camera.zoom();

        for (size_t i = 0; i < n; i++) {
            screenPts[i] = camera.canvasToScreen({pts[i].x, pts[i].y});
            float baseW = s.style.width * widthMult;
            float p = (pts[i].pressure > 0.01f) ? pts[i].pressure : 1.0f;

            float widthFactor = 1.0f;
            if (s.style.penType == 1 && n > 1) { // Fountain / Calligraphy: Directional chisel
                Vec2f dir = (i + 1 < n) ? Vec2f{screenPts[i+1].x - screenPts[i].x, screenPts[i+1].y - screenPts[i].y}
                                        : Vec2f{screenPts[i].x - screenPts[i-1].x, screenPts[i].y - screenPts[i-1].y};
                float angle = std::atan2(dir.y, dir.x);
                widthFactor = 0.35f + 0.65f * std::abs(std::sin(angle - 0.785f));
            }
            halfWs[i] = std::max(1.5f, baseW * zoom * 0.5f * p * widthFactor);
        }

        // Single tap dot — UV maps -1..+1 in both axes for SDF disc
        if (n == 1) {
            Vec2f p = screenPts[0];
            float hw = halfWs[0];
            float q[] = {
                p.x - hw, p.y - hw,  -1.f, -1.f,
                p.x + hw, p.y - hw,   1.f, -1.f,
                p.x - hw, p.y + hw,  -1.f,  1.f,
                p.x + hw, p.y + hw,   1.f,  1.f,
            };
            for (float v : q) discVerts.push_back(v);
            return;
        }

        // 1. Unified Continuous Triangle Strip with Averaged Bisector Normals
        stripVerts.reserve(n * 8);

        std::vector<Vec2f> normals(n);
        for (size_t i = 0; i < n; i++) {
            Vec2f dir{0.f, 0.f};
            if (i == 0) {
                dir = {screenPts[1].x - screenPts[0].x, screenPts[1].y - screenPts[0].y};
            } else if (i == n - 1) {
                dir = {screenPts[n-1].x - screenPts[n-2].x, screenPts[n-1].y - screenPts[n-2].y};
            } else {
                Vec2f d1 = {screenPts[i].x - screenPts[i-1].x, screenPts[i].y - screenPts[i-1].y};
                Vec2f d2 = {screenPts[i+1].x - screenPts[i].x, screenPts[i+1].y - screenPts[i].y};
                float l1 = std::sqrt(d1.x*d1.x + d1.y*d1.y);
                float l2 = std::sqrt(d2.x*d2.x + d2.y*d2.y);
                if (l1 > 0.001f && l2 > 0.001f) {
                    dir = {d1.x / l1 + d2.x / l2, d1.y / l1 + d2.y / l2};
                } else if (l1 > 0.001f) {
                    dir = d1;
                } else {
                    dir = d2;
                }
            }

            float len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
            if (len > 0.001f) {
                normals[i] = {-dir.y / len, dir.x / len};
            } else {
                normals[i] = {0.f, 1.f};
            }
        }

        for (size_t i = 0; i < n; i++) {
            Vec2f p = screenPts[i];
            Vec2f nml = normals[i];
            float hw = halfWs[i];

            // Left vertex (UV.x = -1.0, UV.y = 0.0)
            stripVerts.push_back(p.x - nml.x * hw);
            stripVerts.push_back(p.y - nml.y * hw);
            stripVerts.push_back(-1.0f);
            stripVerts.push_back(0.0f);

            // Right vertex (UV.x = +1.0, UV.y = 0.0)
            stripVerts.push_back(p.x + nml.x * hw);
            stripVerts.push_back(p.y + nml.y * hw);
            stripVerts.push_back(1.0f);
            stripVerts.push_back(0.0f);
        }

        // 2. Round Cap Discs at Start and End of Stroke
        auto addDisc = [&](Vec2f p, float hw) {
            float q[] = {
                p.x - hw, p.y - hw,  -1.f, -1.f,
                p.x + hw, p.y - hw,   1.f, -1.f,
                p.x - hw, p.y + hw,  -1.f,  1.f,
                p.x + hw, p.y + hw,   1.f,  1.f,
            };
            for (float v : q) discVerts.push_back(v);
        };

        addDisc(screenPts[0], halfWs[0]);
        addDisc(screenPts.back(), halfWs.back());
    };

    // For the live stroke, use the dedicated m_liveVbo (GL_STREAM_DRAW) so we
    // don't thrash the general VBO that holds committed strokes.
    GLuint targetVbo = isLiveStroke ? m_liveVbo : m_vbo;
    glBindBuffer(GL_ARRAY_BUFFER, targetVbo);

    auto renderMesh = [&](const std::vector<float>& stripVerts, const std::vector<float>& discVerts) {
        if (!stripVerts.empty()) {
            GLsizeiptr byteSize = (GLsizeiptr)(stripVerts.size() * sizeof(float));
            if (isLiveStroke && byteSize <= m_liveVboSize) {
                // Orphan the old buffer (driver can retire it without stalling) then sub-upload
                glBufferData(GL_ARRAY_BUFFER, m_liveVboSize, nullptr, GL_STREAM_DRAW);
                glBufferSubData(GL_ARRAY_BUFFER, 0, byteSize, stripVerts.data());
            } else {
                glBufferData(GL_ARRAY_BUFFER, byteSize, stripVerts.data(), GL_DYNAMIC_DRAW);
                if (isLiveStroke) m_liveVboSize = byteSize;
            }
            glEnableVertexAttribArray(m_stroke_posLoc);
            glVertexAttribPointer(m_stroke_posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
            glEnableVertexAttribArray(m_stroke_uvLoc);
            glVertexAttribPointer(m_stroke_uvLoc,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)(stripVerts.size() / 4));
        }

        if (!discVerts.empty()) {
            glBufferData(GL_ARRAY_BUFFER, discVerts.size()*sizeof(float), discVerts.data(), GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(m_stroke_posLoc);
            glVertexAttribPointer(m_stroke_posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
            glEnableVertexAttribArray(m_stroke_uvLoc);
            glVertexAttribPointer(m_stroke_uvLoc,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
            int numQuads = (int)discVerts.size() / 16;
            for (int i = 0; i < numQuads; i++) glDrawArrays(GL_TRIANGLE_STRIP, i*4, 4);
        }
    };

    // If Neon Glow Pen (penType == 3), draw Outer Glow Pass first
    if (s.style.penType == 3 && !s.isSelected) {
        glUniform4f(m_stroke_colorLoc,
                    s.style.color.r / 255.f,
                    s.style.color.g / 255.f,
                    s.style.color.b / 255.f,
                    baseAlpha * 0.35f);

        std::vector<float> glowStrip, glowDiscs;
        buildMesh(2.5f, glowStrip, glowDiscs);
        renderMesh(glowStrip, glowDiscs);

        glUniform4f(m_stroke_colorLoc, 1.0f, 1.0f, 1.0f, 0.95f);
    }

    std::vector<float> mainStrip, mainDiscs;
    buildMesh(1.0f, mainStrip, mainDiscs);
    renderMesh(mainStrip, mainDiscs);
}

void GlesRenderPipeline::renderShapes(const Page& page, const CanvasCamera& camera) {
    if (m_strokeProgram == 0) return;
    for (const auto& shape : page.shapes) {
        if (shape.isErased) continue;
        renderShape(shape, camera);
    }
}

void GlesRenderPipeline::renderShape(const ShapeElement& shape, const CanvasCamera& camera) {
    if (m_strokeProgram == 0) return;

    StrokeStyle style;
    style.color = shape.strokeColor;
    style.width = shape.strokeWidth;
    style.opacity = 1.0f;
    style.penType = 0; // Standard crisp vector pen

    Rectf b = shape.bounds;
    float l = std::min(b.left, b.right);
    float r = std::max(b.left, b.right);
    float t = std::min(b.top, b.bottom);
    float bot = std::max(b.top, b.bottom);
    float w = std::max(r - l, 2.0f);
    float h = std::max(bot - t, 2.0f);
    float midX = (l + r) * 0.5f;
    float midY = (t + bot) * 0.5f;

    float cosA = std::cos(shape.rotation);
    float sinA = std::sin(shape.rotation);
    bool hasRot = std::abs(shape.rotation) > 0.001f;

    auto rotPt = [&](Vec2f p) -> Vec2f {
        if (!hasRot) return p;
        float dx = p.x - midX;
        float dy = p.y - midY;
        return { midX + dx * cosA - dy * sinA, midY + dx * sinA + dy * cosA };
    };

    auto drawPolyline = [&](const std::vector<Vec2f>& pts, bool isSelected = false) {
        if (pts.size() < 2) return;
        Stroke s;
        s.style = style;
        s.isSelected = isSelected || shape.isSelected;
        s.points.reserve(pts.size());
        for (const auto& p : pts) {
            Vec2f rp = rotPt(p);
            s.points.push_back({rp.x, rp.y, 1.0f});
        }
        s.updateBounds();
        drawStroke(s, camera);
    };

    auto drawLine = [&](Vec2f p1, Vec2f p2) {
        drawPolyline({p1, p2});
    };

    ShapeType type = static_cast<ShapeType>(shape.shapeType);

    switch (type) {
        case ShapeType::LINE: {
            // Straight line between stored endpoints
            drawPolyline({{b.left, b.top}, {b.right, b.bottom}});
            break;
        }
        case ShapeType::ARROW: {
            Vec2f start{b.left, b.top};
            Vec2f end{b.right, b.bottom};
            drawPolyline({start, end});

            // Arrow head wings at end
            float dx = end.x - start.x;
            float dy = end.y - start.y;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len > 0.001f) {
                float headLen = std::min(30.0f, len * 0.35f);
                float angle = std::atan2(dy, dx);
                float arrowAngle = 0.5f; // ~28 degrees
                Vec2f wing1 = {end.x - headLen * std::cos(angle - arrowAngle),
                               end.y - headLen * std::sin(angle - arrowAngle)};
                Vec2f wing2 = {end.x - headLen * std::cos(angle + arrowAngle),
                               end.y - headLen * std::sin(angle + arrowAngle)};
                drawPolyline({wing1, end, wing2});
            }
            break;
        }
        case ShapeType::RECTANGLE: {
            drawPolyline({{l, t}, {r, t}, {r, bot}, {l, bot}, {l, t}});
            break;
        }
        case ShapeType::CIRCLE: {
            float rx = w * 0.5f;
            float ry = h * 0.5f;
            int segments = 64;
            std::vector<Vec2f> circlePts;
            circlePts.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                circlePts.push_back({midX + rx * std::cos(theta), midY + ry * std::sin(theta)});
            }
            drawPolyline(circlePts);
            break;
        }
        case ShapeType::TRIANGLE: {
            drawPolyline({{midX, t}, {r, bot}, {l, bot}, {midX, t}});
            break;
        }
        case ShapeType::RIGHT_TRIANGLE: {
            drawPolyline({{l, t}, {r, bot}, {l, bot}, {l, t}});
            break;
        }
        case ShapeType::DIAMOND: {
            drawPolyline({{midX, t}, {r, midY}, {midX, bot}, {l, midY}, {midX, t}});
            break;
        }
        case ShapeType::STAR: {
            float R = std::min(w, h) * 0.5f;
            float rInner = R * 0.382f;
            std::vector<Vec2f> starPts;
            starPts.reserve(11);
            for (int i = 0; i <= 10; i++) {
                float rad = (i % 2 == 0) ? R : rInner;
                float angle = (float)i * 3.14159265f / 5.0f - 1.5707963f; // Start at top
                starPts.push_back({midX + rad * std::cos(angle), midY + rad * std::sin(angle)});
            }
            drawPolyline(starPts);
            break;
        }
        case ShapeType::HEXAGON: {
            float rx = w * 0.5f;
            float ry = h * 0.5f;
            std::vector<Vec2f> hexPts;
            hexPts.reserve(7);
            for (int i = 0; i <= 6; i++) {
                float angle = (float)i * 6.2831853f / 6.0f - 1.5707963f;
                hexPts.push_back({midX + rx * std::cos(angle), midY + ry * std::sin(angle)});
            }
            drawPolyline(hexPts);
            break;
        }

        // ── 3D SHAPES ─────────────────────────────────────────────────────────

        case ShapeType::CUBE:
        case ShapeType::CUBOID: {
            float offX = w * 0.28f;
            float offY = h * 0.25f;

            // Front face corners
            Vec2f fTL{l, t + offY};
            Vec2f fTR{r - offX, t + offY};
            Vec2f fBR{r - offX, bot};
            Vec2f fBL{l, bot};

            // Back face corners
            Vec2f bTL{l + offX, t};
            Vec2f bTR{r, t};
            Vec2f bBR{r, bot - offY};
            Vec2f bBL{l + offX, bot - offY};

            // Front face
            drawPolyline({fTL, fTR, fBR, fBL, fTL});
            // Top & right connecting faces
            drawPolyline({fTL, bTL, bTR, fTR});
            drawPolyline({bTR, bBR, fBR});
            // Back edges (wireframe)
            drawPolyline({fBL, bBL, bBR});
            drawPolyline({bTL, bBL});
            break;
        }

        case ShapeType::SPHERE: {
            float rx = w * 0.5f;
            float ry = h * 0.5f;
            int segments = 64;

            // Outer contour circle
            std::vector<Vec2f> contourPts;
            contourPts.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                contourPts.push_back({midX + rx * std::cos(theta), midY + ry * std::sin(theta)});
            }
            drawPolyline(contourPts);

            // Equator ellipse
            std::vector<Vec2f> equatorPts;
            equatorPts.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                equatorPts.push_back({midX + rx * std::cos(theta), midY + ry * 0.35f * std::sin(theta)});
            }
            drawPolyline(equatorPts);

            // Central meridian ellipse
            std::vector<Vec2f> meridianPts;
            meridianPts.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                meridianPts.push_back({midX + rx * 0.35f * std::sin(theta), midY + ry * std::cos(theta)});
            }
            drawPolyline(meridianPts);
            break;
        }

        case ShapeType::CYLINDER: {
            float rx = w * 0.5f;
            float capRy = std::min(h * 0.18f, rx * 0.5f);
            float topY = t + capRy;
            float botY = bot - capRy;
            int segments = 48;

            // Top ellipse
            std::vector<Vec2f> topEllipse;
            topEllipse.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                topEllipse.push_back({midX + rx * std::cos(theta), topY + capRy * std::sin(theta)});
            }
            drawPolyline(topEllipse);

            // Bottom ellipse
            std::vector<Vec2f> botEllipse;
            botEllipse.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                botEllipse.push_back({midX + rx * std::cos(theta), botY + capRy * std::sin(theta)});
            }
            drawPolyline(botEllipse);

            // Side vertical silhouette lines
            drawLine({l, topY}, {l, botY});
            drawLine({r, topY}, {r, botY});
            break;
        }

        case ShapeType::CONE: {
            float rx = w * 0.5f;
            float capRy = std::min(h * 0.18f, rx * 0.5f);
            float botY = bot - capRy;
            Vec2f apex{midX, t};

            // Bottom base ellipse
            int segments = 48;
            std::vector<Vec2f> baseEllipse;
            baseEllipse.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                baseEllipse.push_back({midX + rx * std::cos(theta), botY + capRy * std::sin(theta)});
            }
            drawPolyline(baseEllipse);

            // Apex to left and right silhouette lines
            drawLine(apex, {l, botY});
            drawLine(apex, {r, botY});
            break;
        }

        case ShapeType::FRUSTUM: {
            float topRx = w * 0.32f;
            float topRy = std::min(h * 0.12f, topRx * 0.5f);
            float topY = t + topRy;

            float botRx = w * 0.5f;
            float botRy = std::min(h * 0.18f, botRx * 0.5f);
            float botY = bot - botRy;

            int segments = 48;

            // Top ellipse
            std::vector<Vec2f> topEllipse;
            topEllipse.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                topEllipse.push_back({midX + topRx * std::cos(theta), topY + topRy * std::sin(theta)});
            }
            drawPolyline(topEllipse);

            // Bottom ellipse
            std::vector<Vec2f> botEllipse;
            botEllipse.reserve(segments + 1);
            for (int i = 0; i <= segments; i++) {
                float theta = (float)i * 6.2831853f / (float)segments;
                botEllipse.push_back({midX + botRx * std::cos(theta), botY + botRy * std::sin(theta)});
            }
            drawPolyline(botEllipse);

            // 2 Silhouette lines
            drawLine({midX - topRx, topY}, {l, botY});
            drawLine({midX + topRx, topY}, {r, botY});
            break;
        }

        case ShapeType::PYRAMID: {
            Vec2f apex{midX, t};
            Vec2f bFL{l, bot - h * 0.15f};
            Vec2f bFR{r - w * 0.18f, bot};
            Vec2f bBR{r, bot - h * 0.25f};
            Vec2f bBL{l + w * 0.18f, bot - h * 0.35f};

            // Base perimeter
            drawPolyline({bFL, bFR, bBR, bBL, bFL});

            // 4 Corner edges to apex
            drawLine(apex, bFL);
            drawLine(apex, bFR);
            drawLine(apex, bBR);
            drawLine(apex, bBL);
            break;
        }

        case ShapeType::PRISM: {
            float offX = w * 0.3f;
            float offY = h * 0.25f;

            Vec2f fTop{l + (w - offX) * 0.5f, t + offY};
            Vec2f fBL{l, bot};
            Vec2f fBR{r - offX, bot};

            Vec2f bTop{fTop.x + offX, t};
            Vec2f bBL{fBL.x + offX, bot - offY};
            Vec2f bBR{fBR.x + offX, bot - offY};

            // Front triangle
            drawPolyline({fTop, fBR, fBL, fTop});
            // Top and right connecting ridges
            drawPolyline({fTop, bTop, bBR, fBR});
            // Bottom and back lines
            drawPolyline({fBL, bBL, bBR});
            drawPolyline({bTop, bBL});
            break;
        }
    }
}

void GlesRenderPipeline::renderLasso(const std::vector<Vec2f>& lassoPath, const CanvasCamera& camera) {
    if (lassoPath.size() < 2 || m_strokeProgram == 0) return;

    float mvp[16];
    buildOrthoMat(mvp, 0.f, (float)m_width, (float)m_height, 0.f);

    glUseProgram(m_strokeProgram);
    GLint mvpLoc   = glGetUniformLocation(m_strokeProgram, "uMVP");
    GLint colorLoc = glGetUniformLocation(m_strokeProgram, "uColor");
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

    // Vibrant Orange (#FF9500) for Lasso Dotted Line
    glUniform4f(colorLoc, 1.0f, 0.584f, 0.0f, 1.0f);

    std::vector<float> verts;
    verts.reserve(lassoPath.size() * 16);

    for (size_t i = 0; i + 1 < lassoPath.size(); i += 2) { // Step by 2 for DOTTED line effect!
        Vec2f a = camera.canvasToScreen(lassoPath[i]);
        Vec2f b = camera.canvasToScreen(lassoPath[i+1]);

        float halfW = 3.0f; // 6px thick vibrant orange dots
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 0.001f) continue;

        float px = -dy / len * halfW;
        float py =  dx / len * halfW;

        float q[] = {
            a.x - px, a.y - py,  0.f, 0.f,
            a.x + px, a.y + py,  1.f, 0.f,
            b.x - px, b.y - py,  0.f, 1.f,
            b.x + px, b.y + py,  1.f, 1.f,
        };
        for (float v : q) verts.push_back(v);
    }

    if (verts.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_DYNAMIC_DRAW);

    GLint posLoc = glGetAttribLocation(m_strokeProgram, "aPos");
    GLint uvLoc  = glGetAttribLocation(m_strokeProgram, "aUV");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    glEnableVertexAttribArray(uvLoc);
    glVertexAttribPointer(uvLoc,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));

    int numQuads = (int)verts.size() / 16;
    for (int i = 0; i < numQuads; i++) {
        glDrawArrays(GL_TRIANGLE_STRIP, i*4, 4);
    }
}

void GlesRenderPipeline::renderSelectionBoundingBox(const Rectf& bounds, float rotation, const CanvasCamera& camera) {
    if (m_colorProgram == 0) return;

    float midCanvasX = (bounds.left + bounds.right) * 0.5f;
    float midCanvasY = (bounds.top + bounds.bottom) * 0.5f;
    float hwCanvas   = std::abs(bounds.right - bounds.left) * 0.5f;
    float hhCanvas   = std::abs(bounds.bottom - bounds.top) * 0.5f;

    Vec2f midScreen = camera.canvasToScreen({midCanvasX, midCanvasY});
    float zoom = camera.zoom();
    float pad = 6.0f;
    float screenHw = hwCanvas * zoom + pad;
    float screenHh = hhCanvas * zoom + pad;

    float cosA = std::cos(rotation);
    float sinA = std::sin(rotation);

    auto rotPt = [&](float lx, float ly) -> Vec2f {
        return {
            midScreen.x + lx * cosA - ly * sinA,
            midScreen.y + lx * sinA + ly * cosA
        };
    };

    float mvp[16];
    buildOrthoMat(mvp, 0.f, (float)m_width, (float)m_height, 0.f);

    glUseProgram(m_colorProgram);
    GLint mvpLoc   = glGetUniformLocation(m_colorProgram, "uMVP");
    GLint colorLoc = glGetUniformLocation(m_colorProgram, "uColor");
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
    GLint posLoc   = glGetAttribLocation(m_colorProgram, "aPos");
    glEnableVertexAttribArray(posLoc);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // 1. Gold/Orange Outline Box (#FFB800)
    glUniform4f(colorLoc, 1.0f, 0.72f, 0.0f, 1.0f);
    Vec2f pTL = rotPt(-screenHw, -screenHh);
    Vec2f pTR = rotPt( screenHw, -screenHh);
    Vec2f pBR = rotPt( screenHw,  screenHh);
    Vec2f pBL = rotPt(-screenHw,  screenHh);

    float lineVerts[] = {
        pTL.x, pTL.y,  pTR.x, pTR.y,
        pTR.x, pTR.y,  pBR.x, pBR.y,
        pBR.x, pBR.y,  pBL.x, pBL.y,
        pBL.x, pBL.y,  pTL.x, pTL.y,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(lineVerts), lineVerts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glLineWidth(2.5f);
    glDrawArrays(GL_LINES, 0, 8);

    // 2. 8 Control Handles positioned along rotated frame
    Vec2f handleLocs[8] = {
        pTL, rotPt(0.0f, -screenHh), pTR,
        rotPt(screenHw, 0.0f),
        pBR, rotPt(0.0f, screenHh), pBL,
        rotPt(-screenHw, 0.0f)
    };

    float handleR = 6.0f;
    for (int i = 0; i < 8; i++) {
        Vec2f hp = handleLocs[i];
        // 4 corners of handle box (oriented with rotation)
        Vec2f hTL = { hp.x - handleR * cosA + handleR * sinA, hp.y - handleR * sinA - handleR * cosA };
        Vec2f hTR = { hp.x + handleR * cosA + handleR * sinA, hp.y + handleR * sinA - handleR * cosA };
        Vec2f hBR = { hp.x + handleR * cosA - handleR * sinA, hp.y + handleR * sinA + handleR * cosA };
        Vec2f hBL = { hp.x - handleR * cosA - handleR * sinA, hp.y - handleR * sinA + handleR * cosA };

        // Fill: White
        glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
        float quadVerts[] = {
            hTL.x, hTL.y,
            hTR.x, hTR.y,
            hBL.x, hBL.y,
            hBR.x, hBR.y,
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Border: Gold/Orange (#FFB800)
        glUniform4f(colorLoc, 1.0f, 0.72f, 0.0f, 1.0f);
        float handleBorder[] = {
            hTL.x, hTL.y,  hTR.x, hTR.y,
            hTR.x, hTR.y,  hBR.x, hBR.y,
            hBR.x, hBR.y,  hBL.x, hBL.y,
            hBL.x, hBL.y,  hTL.x, hTL.y,
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(handleBorder), handleBorder, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glDrawArrays(GL_LINES, 0, 8);
    }

    // 3. Rotation Handle Stem & Handle
    Vec2f pTC  = rotPt(0.0f, -screenHh);
    Vec2f pRot = rotPt(0.0f, -screenHh - 35.0f);

    // Stem line from Top-Center to Rotation Handle
    glUniform4f(colorLoc, 1.0f, 0.72f, 0.0f, 1.0f);
    float stemVerts[] = { pTC.x, pTC.y, pRot.x, pRot.y };
    glBufferData(GL_ARRAY_BUFFER, sizeof(stemVerts), stemVerts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINES, 0, 2);

    // Rotation Handle (Gold Fill with White Center)
    glUniform4f(colorLoc, 1.0f, 0.72f, 0.0f, 1.0f);
    Vec2f rTL = { pRot.x - handleR * cosA + handleR * sinA, pRot.y - handleR * sinA - handleR * cosA };
    Vec2f rTR = { pRot.x + handleR * cosA + handleR * sinA, pRot.y + handleR * sinA - handleR * cosA };
    Vec2f rBR = { pRot.x + handleR * cosA - handleR * sinA, pRot.y + handleR * sinA + handleR * cosA };
    Vec2f rBL = { pRot.x - handleR * cosA - handleR * sinA, pRot.y - handleR * sinA + handleR * cosA };

    float rotHandleVerts[] = {
        rTL.x, rTL.y,
        rTR.x, rTR.y,
        rBL.x, rBL.y,
        rBR.x, rBR.y,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(rotHandleVerts), rotHandleVerts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    float inR = handleR * 0.5f;
    Vec2f riTL = { pRot.x - inR * cosA + inR * sinA, pRot.y - inR * sinA - inR * cosA };
    Vec2f riTR = { pRot.x + inR * cosA + inR * sinA, pRot.y + inR * sinA - inR * cosA };
    Vec2f riBR = { pRot.x + inR * cosA - inR * sinA, pRot.y + inR * sinA + inR * cosA };
    Vec2f riBL = { pRot.x - inR * cosA - inR * sinA, pRot.y - inR * sinA + inR * cosA };

    float rotInner[] = {
        riTL.x, riTL.y,
        riTR.x, riTR.y,
        riBL.x, riBL.y,
        riBR.x, riBR.y,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(rotInner), rotInner, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GlesRenderPipeline::renderEraserCircle(Vec2f screenPt, float screenRadius) {
    if (m_colorProgram == 0 || screenRadius < 1.0f) return;

    const int segments = 36;
    std::vector<float> discVerts;
    discVerts.reserve((segments + 2) * 2);

    discVerts.push_back(screenPt.x);
    discVerts.push_back(screenPt.y);

    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / (float)segments * 6.28318530718f;
        discVerts.push_back(screenPt.x + std::cos(angle) * screenRadius);
        discVerts.push_back(screenPt.y + std::sin(angle) * screenRadius);
    }

    float mvp[16];
    buildOrthoMat(mvp, 0.f, (float)m_width, (float)m_height, 0.f);

    glUseProgram(m_colorProgram);
    GLint mvpLoc   = glGetUniformLocation(m_colorProgram, "uMVP");
    GLint colorLoc = glGetUniformLocation(m_colorProgram, "uColor");
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    GLint posLoc = glGetAttribLocation(m_colorProgram, "aPos");
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);

    // 1. Semi-transparent clean white fill
    glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 0.50f);
    glBufferData(GL_ARRAY_BUFFER, discVerts.size() * sizeof(float), discVerts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);

    // 2. Crisp circle outline
    glUniform4f(colorLoc, 0.20f, 0.20f, 0.25f, 0.85f);
    glDrawArrays(GL_LINE_LOOP, 1, segments);
}

void GlesRenderPipeline::renderImages(const Page& page, const CanvasCamera& camera) {
    if (page.images.empty() || m_texProgram == 0) return;

    float mvp[16];
    buildOrthoMat(mvp, 0.f, (float)m_width, (float)m_height, 0.f);

    glUseProgram(m_texProgram);
    GLint mvpLoc = glGetUniformLocation(m_texProgram, "uMVP");
    GLint texLoc = glGetUniformLocation(m_texProgram, "uTex");
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
    glUniform1i(texLoc, 0);

    GLint posLoc = glGetAttribLocation(m_texProgram, "aPos");
    GLint uvLoc  = glGetAttribLocation(m_texProgram, "aUV");
    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(uvLoc);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glActiveTexture(GL_TEXTURE0);

    for (const auto& img : page.images) {
        if (img.isErased || img.textureId == 0) continue;

        Rectf screenRect = camera.canvasToScreenRect(img.bounds);

        // Viewport frustum culling
        if (screenRect.right < 0 || screenRect.left > (float)m_width ||
            screenRect.bottom < 0 || screenRect.top > (float)m_height) {
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, (GLuint)img.textureId);

        float u0 = img.flipH ? 1.0f : 0.0f;
        float u1 = img.flipH ? 0.0f : 1.0f;
        float v0 = img.flipV ? 1.0f : 0.0f;
        float v1 = img.flipV ? 0.0f : 1.0f;

        float quadVerts[16];
        if (std::abs(img.rotation) > 0.001f) {
            float cx = (screenRect.left + screenRect.right) * 0.5f;
            float cy = (screenRect.top + screenRect.bottom) * 0.5f;
            float hw = (screenRect.right - screenRect.left) * 0.5f;
            float hh = (screenRect.bottom - screenRect.top) * 0.5f;

            float cosA = std::cos(img.rotation);
            float sinA = std::sin(img.rotation);

            auto rotPt = [&](float rx, float ry) -> Vec2f {
                return { cx + rx * cosA - ry * sinA, cy + rx * sinA + ry * cosA };
            };

            Vec2f pTL = rotPt(-hw, -hh);
            Vec2f pTR = rotPt( hw, -hh);
            Vec2f pBL = rotPt(-hw,  hh);
            Vec2f pBR = rotPt( hw,  hh);

            quadVerts[0] = pTL.x; quadVerts[1] = pTL.y; quadVerts[2] = u0; quadVerts[3] = v0;
            quadVerts[4] = pTR.x; quadVerts[5] = pTR.y; quadVerts[6] = u1; quadVerts[7] = v0;
            quadVerts[8] = pBL.x; quadVerts[9] = pBL.y; quadVerts[10]= u0; quadVerts[11]= v1;
            quadVerts[12]= pBR.x; quadVerts[13]= pBR.y; quadVerts[14]= u1; quadVerts[15]= v1;
        } else {
            quadVerts[0] = screenRect.left;  quadVerts[1] = screenRect.top;    quadVerts[2] = u0; quadVerts[3] = v0;
            quadVerts[4] = screenRect.right; quadVerts[5] = screenRect.top;    quadVerts[6] = u1; quadVerts[7] = v0;
            quadVerts[8] = screenRect.left;  quadVerts[9] = screenRect.bottom; quadVerts[10]= u0; quadVerts[11]= v1;
            quadVerts[12]= screenRect.right; quadVerts[13]= screenRect.bottom; quadVerts[14]= u1; quadVerts[15]= v1;
        }

        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(uvLoc,  2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisableVertexAttribArray(uvLoc);
    glDisableVertexAttribArray(posLoc);
}

void GlesRenderPipeline::setLiveStroke(const Stroke* liveStroke) {
    m_liveStroke = liveStroke;
}

uint32_t GlesRenderPipeline::uploadTexture(const uint8_t* pixels, int32_t w, int32_t h, bool hasAlpha) {
    if (!m_initialized || !pixels || w <= 0 || h <= 0) return 0;
    if (m_display != EGL_NO_DISPLAY && m_surface != EGL_NO_SURFACE && m_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLenum fmt = hasAlpha ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
    return (uint32_t)tex;
}

void GlesRenderPipeline::deleteTexture(uint32_t handle) {
    GLuint tex = (GLuint)handle;
    glDeleteTextures(1, &tex);
}

GLuint GlesRenderPipeline::compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        LOGE("Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint GlesRenderPipeline::linkProgram(GLuint vert, GLuint frag) {
    if (vert == 0 || frag == 0) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        LOGE("Program link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void GlesRenderPipeline::checkGLError(const char* tag) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) LOGE("GL error [%s]: 0x%x", tag, err);
}

// ─── cacheShaderLocations ─────────────────────────────────────────────────────
// Called once after all shader programs are linked. Stores every uniform and
// attrib location in member variables so drawStroke() and friends never need to
// call glGetUniformLocation / glGetAttribLocation at runtime.
void GlesRenderPipeline::cacheShaderLocations() {
    // Stroke program
    if (m_strokeProgram) {
        m_stroke_mvpLoc   = glGetUniformLocation(m_strokeProgram, "uMVP");
        m_stroke_colorLoc = glGetUniformLocation(m_strokeProgram, "uColor");
        m_stroke_posLoc   = glGetAttribLocation (m_strokeProgram, "aPos");
        m_stroke_uvLoc    = glGetAttribLocation (m_strokeProgram, "aUV");
    }
    // Color program
    if (m_colorProgram) {
        m_color_mvpLoc    = glGetUniformLocation(m_colorProgram, "uMVP");
        m_color_colorLoc  = glGetUniformLocation(m_colorProgram, "uColor");
        m_color_posLoc    = glGetAttribLocation (m_colorProgram, "aPos");
    }
    // Texture program
    if (m_texProgram) {
        m_tex_mvpLoc      = glGetUniformLocation(m_texProgram, "uMVP");
        m_tex_texLoc      = glGetUniformLocation(m_texProgram, "uTex");
        m_tex_posLoc      = glGetAttribLocation (m_texProgram, "aPos");
        m_tex_uvLoc       = glGetAttribLocation (m_texProgram, "aUV");
    }
}

} // namespace ob
