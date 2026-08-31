# 03 — Rendering Pipeline

> **OpenBoard Whiteboard Engine — Internal Architecture Documentation**
> Audience: Engine contributors and graphics subsystem developers.

---

## Table of Contents

1. [Rendering Philosophy](#1-rendering-philosophy)
2. [Skia Library Overview](#2-skia-library-overview)
3. [GPU Initialization on Android](#3-gpu-initialization-on-android)
4. [The Render Loop](#4-the-render-loop)
5. [R-Tree Spatial Index for Viewport Culling](#5-r-tree-spatial-index-for-viewport-culling)
6. [Tile-Based Rendering](#6-tile-based-rendering)
7. [Layer Compositing](#7-layer-compositing)
8. [Stroke Rendering](#8-stroke-rendering)
9. [Anti-Aliasing Strategy](#9-anti-aliasing-strategy)
10. [Caching: SkPicture and SkImage](#10-caching-skpicture-and-skimage)
11. [Frame Timing](#11-frame-timing)
12. [Zoom Level and Rendering Quality](#12-zoom-level-and-rendering-quality)
13. [Thumbnail Generation](#13-thumbnail-generation)
14. [Screenshot / Export Pipeline](#14-screenshot--export-pipeline)
15. [SkPaint Configuration per Tool](#15-skpaint-configuration-per-tool)

---

## 1. Rendering Philosophy

### Every Frame Is Independent

OpenBoard follows a **stateless, full-redraw rendering model** in its first pass. Every time the display needs to be updated, the entire visible portion of the canvas is redrawn from scratch. This is a deliberate architectural decision that trades raw GPU bandwidth for simplicity, correctness, and predictability.

**Why no dirty-region tracking in the first pass?**

| Approach | Pros | Cons |
|---|---|---|
| Dirty-rect invalidation | Less GPU work per frame | Complex state tracking, race conditions with async input |
| Full redraw (chosen) | Simple, always correct, easy to reason about | Higher baseline GPU cost |

The incremental dirty-tracking optimization (phase 2) is added as a **layer on top** of the full-redraw model — the underlying pipeline does not need to be aware of it. This keeps the renderer correct by default, with optimizations being opt-in.

**Design principles:**

- No global mutable render state. Each frame is a pure function of canvas state + viewport.
- The renderer does not own scene data; it reads from an immutable snapshot captured at frame start.
- All GPU commands are issued from a single dedicated render thread.
- The CPU thread prepares the command list; the GPU thread executes it.

```
[Main Thread]         [Render Thread]         [GPU]
    |                      |                    |
  Input event              |                    |
  Update model             |                    |
  Capture snapshot ------> |                    |
                     Cull objects               |
                     Sort by z-order            |
                     Issue SkCanvas calls -----> Skia GPU backend
                     Flush GrDirectContext ----> Execute on GPU
                                                Present frame
```

### Immutable Frame Snapshot

At the start of each frame, the render thread captures a **FrameSnapshot**:

```cpp
struct FrameSnapshot {
    Viewport                   viewport;      // Current pan + zoom
    std::vector<LayerSnapshot> layers;        // Ordered list of layers
    uint64_t                   frameNumber;   // Monotonic counter
    double                     timestampMs;   // Wall clock at capture
    RenderQuality              quality;       // Normal / Draft / Export
};

struct LayerSnapshot {
    LayerID                  id;
    float                    alpha;           // Layer opacity 0.0-1.0
    BlendMode                blendMode;
    bool                     visible;
    std::vector<DrawableRef> drawables;       // Shared-ptr to immutable objects
};
```

Once captured, this snapshot is handed to the render thread. The main thread can proceed with the next input event without any lock contention.

---

## 2. Skia Library Overview

### Why Skia?

[Skia](https://skia.org) is Google's open-source 2D graphics library, used in Chrome, Android, Flutter, and many other products. It was chosen for OpenBoard for the following reasons:

| Criterion | Skia | Alternative (Cairo, AGG, NanoVG) |
|---|---|---|
| GPU acceleration | First-class via Ganesh/Graphite | Limited or none |
| Platform support | Android, iOS, Linux, Windows, macOS | Varies |
| Text rendering | Excellent (HarfBuzz + FreeType) | Varies |
| Path quality | Industry-leading anti-aliasing | Good |
| Active development | Yes (Google/Flutter team) | Mostly stagnant |
| Pressure-sensitive strokes | Supported via variable-width path | Limited |

### GrDirectContext

The root GPU context. Owns the GPU resource cache, manages command buffer submission, and controls surface allocation. There is exactly **one** `GrDirectContext` per GL/Vulkan context.

```cpp
// Creation (OpenGL ES backend)
sk_sp<GrDirectContext> grContext = GrDirectContext::MakeGL(
    GrGLMakeNativeInterface(),
    grContextOptions
);

// Resource limits
grContext->setResourceCacheLimit(256 * 1024 * 1024); // 256 MB GPU cache
```

> **Important:** `GrDirectContext` is **not thread-safe**. All calls must originate from the render thread that owns the GL context.

### SkSurface

Represents a render target — either on-screen (wrapping the default framebuffer) or off-screen (texture-backed). Every layer gets its own `SkSurface`.

```cpp
// On-screen surface wrapping EGL default framebuffer
GrGLFramebufferInfo fbInfo;
fbInfo.fFBOID   = 0;                       // Default framebuffer
fbInfo.fFormat  = GL_RGBA8;

GrBackendRenderTarget backendRT(
    screenWidth, screenHeight,
    sampleCount,                           // MSAA samples (0=none, 4=4x)
    stencilBits,
    fbInfo
);

sk_sp<SkSurface> onscreenSurface = SkSurface::MakeFromBackendRenderTarget(
    grContext.get(), backendRT,
    kBottomLeft_GrSurfaceOrigin,           // OpenGL origin is bottom-left
    kRGBA_8888_SkColorType, colorSpace, &surfaceProps
);

// Off-screen layer surface (texture-backed)
sk_sp<SkSurface> layerSurface = SkSurface::MakeRenderTarget(
    grContext.get(), SkBudgeted::kYes,
    SkImageInfo::MakeN32Premul(width, height),
    sampleCount, kTopLeft_GrSurfaceOrigin, &surfaceProps
);
```

### SkCanvas

The drawing API surface. Obtained from `SkSurface::getCanvas()`. Provides the full 2D drawing API: paths, text, images, transforms, clips.

| Operation | Method | Used For |
|---|---|---|
| Draw path | `canvas->drawPath(path, paint)` | Strokes, shapes |
| Draw image | `canvas->drawImage(image, x, y)` | Tiles, stickers, photos |
| Draw text | `canvas->drawTextBlob(blob, x, y, paint)` | Text elements |
| Clip rect | `canvas->clipRect(rect)` | Viewport clipping |
| Save/restore | `canvas->save()` / `canvas->restore()` | Layer transforms |
| Concat matrix | `canvas->concat(matrix)` | Camera + layer transforms |

---

## 3. GPU Initialization on Android

### EGL + OpenGL ES 3.0 Path

The primary rendering path on Android uses EGL to create an OpenGL ES 3.0 context.

```
Android SurfaceView -> ANativeWindow -> EGLDisplay (eglGetDisplay)
    -> EGLSurface (eglCreateWindowSurface)
    -> EGLContext (eglCreateContext, ES 3.0)
    -> GrDirectContext (MakeGL)
    -> SkSurface (wraps default FBO 0)
```

**Full initialization sequence:**

```cpp
// Step 1: Get EGL display
EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
eglInitialize(display, &major, &minor);

// Step 2: Choose config
const EGLint configAttribs[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RED_SIZE,   8,  EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,  EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 0,
    EGL_STENCIL_SIZE, 8,    // Required by Skia for clipping
    EGL_NONE
};
EGLConfig config;
EGLint numConfigs;
eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);

// Step 3: Create window surface
EGLSurface surface = eglCreateWindowSurface(display, config, nativeWindow, nullptr);

// Step 4: Create ES 3.0 context
const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
eglMakeCurrent(display, surface, surface, context);

// Step 5: Create Skia GrDirectContext
GrContextOptions options;
options.fGpuBudgetedResourceCacheLimit = 256 * 1024 * 1024;
options.fReduceOpsTaskSplitting = GrContextOptions::Enable::kYes;

sk_sp<GrDirectContext> grCtx =
    GrDirectContext::MakeGL(GrGLMakeNativeInterface(), options);
```

### Front-Buffer Rendering for Low-Latency Stylus

For low-latency stylus rendering, OpenBoard uses the `EGL_ANDROID_front_buffer_auto_refresh` extension when available. This allows drawing directly to the front buffer, skipping the swap entirely for each stroke point:

```cpp
const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
bool hasFrontBuffer = strstr(extensions, "EGL_ANDROID_front_buffer_auto_refresh");

if (hasFrontBuffer) {
    eglSurfaceAttrib(display, surface,
        EGL_FRONT_BUFFER_AUTO_REFRESH_ANDROID, EGL_TRUE);
    // Stroke points render directly to display without eglSwapBuffers
}
```

### Vulkan Path

On Android 8.0+ devices with Vulkan 1.1 support, OpenBoard can use the Skia Vulkan backend for lower CPU overhead and better driver multi-threading.

```
VkInstance -> VkPhysicalDevice -> VkDevice
     |
  VkQueue (graphics) + VkQueue (transfer)
     |
GrDirectContext::MakeVulkan(backendContext)
     |
SkSurface (backed by VkImage in swapchain)
```

```cpp
bool tryVulkan() {
    if (__builtin_available(android 28, *)) {
        void* lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        auto fn = (PFN_vkEnumerateInstanceVersion)
            dlsym(lib, "vkEnumerateInstanceVersion");
        if (!fn) return false;
        uint32_t version;
        fn(&version);
        return VK_VERSION_MAJOR(version) >= 1 && VK_VERSION_MINOR(version) >= 1;
    }
    return false;
}

// Backend selection at startup
RenderBackend backend = tryVulkan() ? RenderBackend::Vulkan
                                    : RenderBackend::OpenGLES;
```

```cpp
GrVkBackendContext vkCtx{};
vkCtx.fInstance           = instance;
vkCtx.fPhysicalDevice     = physDevice;
vkCtx.fDevice             = device;
vkCtx.fQueue              = graphicsQueue;
vkCtx.fGraphicsQueueIndex = graphicsQueueIndex;
vkCtx.fMaxAPIVersion      = VK_API_VERSION_1_1;
vkCtx.fVkExtensions       = &extensions;
vkCtx.fDeviceFeatures2    = &features2;

sk_sp<GrDirectContext> grCtx = GrDirectContext::MakeVulkan(vkCtx, grOptions);
```

---

## 4. The Render Loop

The render loop runs on a dedicated **RenderThread**, pinned to a performance CPU core. It is driven by Android's `Choreographer` callback, ensuring frames are submitted in sync with the display refresh (VSync).

```
Choreographer::doFrame(frameTimeNanos)
    |
RenderThread::onVSync(frameTimeNs)
    |
1.  Acquire frame snapshot (lock-free atomic copy)
2.  Collect visible objects  (R-Tree spatial query per visible layer)
3.  Sort by layer index, then z-order within layer
4.  For each layer (bottom to top):
    a.  Acquire layer SkSurface from pool
    b.  Clear surface to transparent
    c.  Apply camera matrix (pan + zoom)
    d.  Draw each visible drawable (stroke, shape, image, text)
    e.  Release surface back to pool after compositing
5.  Composite layers onto on-screen surface (alpha, blend mode)
6.  Draw UI overlay: selection boxes, transform handles, guidelines
7.  grContext->flush() + grContext->submit()
8.  eglSwapBuffers() or front-buffer present
9.  Record frame time in profiler
```

### Collect Visible Objects

```cpp
std::vector<DrawableRef> collectVisible(const FrameSnapshot& snap) {
    SkRect worldViewport = snap.viewport.toWorldRect();
    std::vector<DrawableRef> visible;
    for (const auto& layer : snap.layers) {
        if (!layer.visible) continue;
        auto hits = spatialIndex.query(worldViewport, layer.id);
        visible.insert(visible.end(), hits.begin(), hits.end());
    }
    return visible;
}
```

### Sort by Layer and Z-Order

```cpp
std::sort(visible.begin(), visible.end(),
    [](const DrawableRef& a, const DrawableRef& b) {
        if (a->layerIndex != b->layerIndex)
            return a->layerIndex < b->layerIndex;
        return a->zOrder < b->zOrder;
    });
```

### Draw and Layer-Composite

```cpp
void drawFrame(const FrameSnapshot& snap) {
    SkCanvas* screen = onscreenSurface->getCanvas();
    screen->clear(SK_ColorWHITE);              // Page background
    SkMatrix cam = snap.viewport.toSkMatrix(); // pan + zoom matrix

    for (const auto& layer : snap.layers) {
        if (!layer.visible) continue;

        SkSurface* layerSurf   = getOrCreateLayerSurface(layer.id);
        SkCanvas*  layerCanvas = layerSurf->getCanvas();
        layerCanvas->clear(SK_ColorTRANSPARENT);
        layerCanvas->setMatrix(cam);

        for (const auto& drawable : layer.drawables) {
            drawable->draw(layerCanvas);       // Each object draws itself
        }

        // Composite this layer onto the screen surface
        SkPaint cp;
        cp.setAlphaf(layer.alpha);
        cp.setBlendMode(toSkBlendMode(layer.blendMode));
        screen->drawImage(layerSurf->makeImageSnapshot(), 0, 0, &cp);
    }
}
```

### Flush and Present

```cpp
grContext->flush();               // Submit all pending GPU commands
grContext->submit();              // Ensure commands are queued on GPU
eglSwapBuffers(eglDisplay, eglSurface);
frameProfiler.recordFrame(frameTimeMs);
```

---

## 5. R-Tree Spatial Index for Viewport Culling

### Purpose

At any zoom level, the user sees only a small fraction of the canvas. For a canvas with 10,000 strokes, it would be wasteful to issue draw calls for all of them every frame. The **R-Tree spatial index** allows O(log n + k) retrieval of all objects that intersect the current viewport, where k is the number of results.

### R-Tree Structure

An R-Tree organizes axis-aligned bounding boxes (AABBs) into a balanced tree. Each leaf node stores a drawable's bounding box and its ID. Internal nodes store the minimum bounding rectangle (MBR) of all their children.

```
        [Root MBR: entire canvas extent]
       /                              \
  [MBR A: top-left region]     [MBR B: bottom-right region]
  /              \              /                   \
[s1: stroke]  [s2: rect]   [s3: text]          [s4: image]
```

### R-Tree Implementation

```cpp
// RTree.h — template spatial index
template<typename T, int MinChildren = 2, int MaxChildren = 9>
class RTree {
public:
    using BoundingBox = SkRect;

    void           insert(const BoundingBox& bbox, T id);
    void           remove(const BoundingBox& bbox, T id);
    void           update(const BoundingBox& oldBox,
                          const BoundingBox& newBox, T id);
    std::vector<T> query(const BoundingBox& viewport) const;

    // Bulk-load from pre-sorted data using STR packing (Sort-Tile-Recursive)
    // Produces a much better tree than sequential inserts for large datasets
    static RTree bulkLoad(std::vector<std::pair<BoundingBox, T>>& entries);

private:
    struct Node {
        BoundingBox mbr;
        bool        isLeaf;
        union {
            std::array<Node*, MaxChildren> children; // internal node
            struct { BoundingBox bbox; T id; } entry; // leaf node
        };
        int childCount = 0;
    };
    Node* root = nullptr;

    Node* chooseLeaf(const BoundingBox& bbox);
    void  adjustTree(Node* leaf, Node* newSibling);
    Node* splitNode(Node* node, const BoundingBox& bbox, T id);
};
```

### Per-Layer Spatial Indices

Each layer maintains its own R-Tree, allowing the viewport query to be scoped to visible layers only:

```cpp
class SpatialIndex {
    std::unordered_map<LayerID, RTree<DrawableID>> layerTrees;

public:
    std::vector<DrawableRef> query(const SkRect& viewport, LayerID layer) {
        auto it = layerTrees.find(layer);
        if (it == layerTrees.end()) return {};
        auto ids = it->second.query(viewport);
        return resolveDrawables(ids);    // map DrawableID -> shared_ptr
    }

    void insert(LayerID layer, DrawableID id, const SkRect& bbox) {
        layerTrees[layer].insert(bbox, id);
    }

    void remove(LayerID layer, DrawableID id, const SkRect& bbox) {
        layerTrees[layer].remove(bbox, id);
    }
};
```

### Stroke Bounding Box Computation

For strokes, the AABB must account for the maximum brush radius:

```cpp
SkRect computeStrokeBBox(const Stroke& stroke) {
    SkRect pathBounds = stroke.path.getBounds();
    float  halfWidth  = stroke.maxWidth / 2.0f;
    // Outset by half-width to account for stroke caps and pressure variation
    return pathBounds.makeOutset(halfWidth, halfWidth);
}
```

### Performance Characteristics

| Operation | Complexity | Trigger |
|---|---|---|
| Insert | O(log n) | After each stroke commit |
| Delete | O(log n) | After erase/undo |
| Update | O(2 log n) | After object transform |
| Query | O(log n + k) | Every rendered frame |
| Bulk load | O(n log n) | Document open |

---

## 6. Tile-Based Rendering

### Problem: Very Large Canvases

Infinite canvas mode allows the user to scroll arbitrarily far. At 10% zoom, the visible world area is **100x larger** than the screen. Rendering the entire visible area into a single SkSurface at screen resolution would require enormous GPU memory and prohibitively long draw calls.

### Solution: 512 x 512 World-Unit Tiles

The canvas is divided into a uniform grid of **512 x 512 world-unit tiles**. Each tile is rendered independently and cached as a GPU-resident `SkImage`. At any given time, only the tiles that overlap the current viewport are rendered and uploaded.

```
World Space Grid (each cell = 512x512 WU):

+------+------+------+------+------+
|(0,0) |(1,0) |(2,0) |(3,0) |(4,0) |
+------+------+------+------+------+
|(0,1) |(1,1) |(2,1) |(3,1) |(4,1) |  <- Viewport overlaps (1,1),(2,1),(1,2),(2,2)
+------+------+------+------+------+      Only these 4 tiles are rendered this frame
|(0,2) |(1,2) |(2,2) |(3,2) |(4,2) |
+------+------+------+------+------+
```

### Tile Coordinate System

```cpp
static constexpr float TILE_SIZE_WU = 512.0f;  // 512 world units per tile side

struct TileCoord {
    int32_t x;     // Column index (can be negative for left of origin)
    int32_t y;     // Row index (can be negative for above origin)
    int32_t zoom;  // LOD level: 0=full res, 1=2x downsampled, 2=4x downsampled
};

inline TileCoord worldToTile(SkPoint worldPt, int zoom = 0) {
    float tileSize = TILE_SIZE_WU * (1 << zoom);
    return {
        (int32_t)std::floor(worldPt.x() / tileSize),
        (int32_t)std::floor(worldPt.y() / tileSize),
        zoom
    };
}

inline SkRect tileWorldRect(TileCoord tc) {
    float tileSize = TILE_SIZE_WU * (1 << tc.zoom);
    return SkRect::MakeXYWH(
        tc.x * tileSize, tc.y * tileSize,
        tileSize, tileSize
    );
}

// Get all tiles overlapping a world-space rect at given LOD
std::vector<TileCoord> tilesForRect(const SkRect& worldRect, int zoom) {
    TileCoord minTc = worldToTile({worldRect.left(), worldRect.top()}, zoom);
    TileCoord maxTc = worldToTile({worldRect.right(), worldRect.bottom()}, zoom);
    std::vector<TileCoord> result;
    for (int x = minTc.x; x <= maxTc.x; x++)
    for (int y = minTc.y; y <= maxTc.y; y++)
        result.push_back({x, y, zoom});
    return result;
}
```

### Tile Cache Implementation

```cpp
class TileCache {
    static constexpr int    TILE_PX_SIZE       = 512;   // pixels per tile
    static constexpr size_t MAX_TILE_MEMORY_MB = 256;   // GPU budget

    struct TileEntry {
        sk_sp<SkImage> gpuImage;        // GPU-resident (texture)
        uint64_t       contentHash;     // Hash of drawables that contributed
        uint64_t       lastUsedFrame;   // For LRU eviction
        bool           dirty;           // Needs re-render
    };

    std::unordered_map<TileCoord, TileEntry, TileCoordHash> cache;

public:
    // Returns cached tile, or nullptr if dirty/missing
    sk_sp<SkImage> getTile(TileCoord tc) {
        auto it = cache.find(tc);
        if (it == cache.end() || it->second.dirty) return nullptr;
        it->second.lastUsedFrame = currentFrame;
        return it->second.gpuImage;
    }

    // Render tile from scratch and cache result
    sk_sp<SkImage> renderTile(TileCoord tc, GrDirectContext* grCtx,
                               const SpatialIndex& idx)
    {
        SkRect tileRect = tileWorldRect(tc);

        // Allocate offscreen surface (GPU-backed)
        auto surf = SkSurface::MakeRenderTarget(
            grCtx, SkBudgeted::kYes,
            SkImageInfo::MakeN32Premul(TILE_PX_SIZE, TILE_PX_SIZE)
        );
        SkCanvas* c = surf->getCanvas();
        c->clear(SK_ColorTRANSPARENT);

        // Map tile world coords -> tile pixel coords
        float scale = (float)TILE_PX_SIZE / (TILE_SIZE_WU * (1 << tc.zoom));
        SkMatrix m  = SkMatrix::Scale(scale, scale);
        m.preTranslate(-tileRect.left(), -tileRect.top());
        c->setMatrix(m);

        // Draw all objects whose bounding boxes overlap this tile
        for (auto& drawable : idx.query(tileRect, ALL_LAYERS)) {
            drawable->draw(c);
        }

        auto img = surf->makeImageSnapshot();
        evictIfNeeded();
        cache[tc] = { img, computeContentHash(tc, idx), currentFrame, false };
        return img;
    }

    // Mark all tiles overlapping worldRect as needing re-render
    void invalidateRect(const SkRect& worldRect) {
        for (int zoom = 0; zoom <= 2; zoom++) {
            for (auto& tc : tilesForRect(worldRect, zoom)) {
                auto it = cache.find(tc);
                if (it != cache.end()) it->second.dirty = true;
            }
        }
    }

private:
    void evictIfNeeded() {
        while (estimatedMemoryUsageMB() > MAX_TILE_MEMORY_MB) {
            // Find tile with oldest lastUsedFrame that is not currently visible
            auto oldest = std::min_element(cache.begin(), cache.end(),
                [](const auto& a, const auto& b) {
                    return a.second.lastUsedFrame < b.second.lastUsedFrame;
                });
            if (oldest != cache.end()) cache.erase(oldest);
        }
    }
};
```

### LOD (Level of Detail) Selection

| LOD | World Units / Tile | Pixel Coverage | Active When |
|---|---|---|---|
| zoom=0 | 512 x 512 WU | Full resolution | Zoom >= 50% |
| zoom=1 | 1024 x 1024 WU | 2x downsampled | Zoom 25%-50% |
| zoom=2 | 2048 x 2048 WU | 4x downsampled | Zoom < 25% |

```cpp
int selectLOD(float zoomFactor) {
    if (zoomFactor >= 0.5f)  return 0;   // Full detail
    if (zoomFactor >= 0.25f) return 1;   // Half detail
    return 2;                             // Quarter detail
}
```

---

## 7. Layer Compositing

### Layer Model

Each layer is an independent drawing surface. Layers are composited in order from bottom to top. Each layer has:

- **Alpha** (0.0-1.0): Overall layer opacity
- **Blend mode**: How this layer mixes with the composite of layers below it
- **SkSurface**: The GPU texture this layer renders into

### Supported Blend Modes

| Name | SkBlendMode | Description |
|---|---|---|
| Normal | `kSrcOver` | Standard alpha compositing (Porter-Duff over) |
| Multiply | `kMultiply` | Darkens — simulates ink on paper |
| Screen | `kScreen` | Lightens — used for glow/highlights |
| Overlay | `kOverlay` | Contrast-enhancing (darken darks, lighten lights) |
| Darken | `kDarken` | Takes per-channel darker of src and dst |
| Lighten | `kLighten` | Takes per-channel lighter of src and dst |
| Color Dodge | `kColorDodge` | Brightens dst by src |
| Color Burn | `kColorBurn` | Darkens dst by src |
| Hard Light | `kHardLight` | Hard contrast overlay |
| Erase | `kDstOut` | Subtractive erase (punches through lower layers) |

### Compositing Pass

```cpp
void compositeLayers(SkCanvas* screen, const std::vector<LayerSurface>& layers) {
    SkSamplingOptions sampling(SkFilterMode::kLinear, SkMipmapMode::kNearest);

    for (const auto& layer : layers) {
        if (!layer.visible || layer.alpha < 0.001f) continue;

        // Snapshot the layer surface as a GPU image
        sk_sp<SkImage> layerImg = layer.surface->makeImageSnapshot();

        SkPaint p;
        p.setAlphaf(layer.alpha);
        p.setBlendMode(layer.blendMode);
        p.setAntiAlias(false);       // No AA needed on full-resolution blit

        screen->drawImage(layerImg, 0, 0, sampling, &p);
    }
}
```

### Layer Surface Pool

Creating and destroying GPU textures is expensive (involves GPU sync). OpenBoard maintains a **surface pool** to reuse layer textures across frames:

```cpp
class LayerSurfacePool {
    struct PooledSurface {
        sk_sp<SkSurface> surface;
        SkISize          size;
        bool             inUse = false;
    };
    std::vector<PooledSurface> pool;

public:
    sk_sp<SkSurface> acquire(GrDirectContext* grCtx, int w, int h) {
        // Find an idle surface of matching size
        for (auto& ps : pool) {
            if (!ps.inUse && ps.size == SkISize{w, h}) {
                ps.inUse = true;
                return ps.surface;
            }
        }
        // Allocate new surface
        auto surf = SkSurface::MakeRenderTarget(
            grCtx, SkBudgeted::kYes,
            SkImageInfo::MakeN32Premul(w, h),
            4,                          // 4x MSAA
            kTopLeft_GrSurfaceOrigin, nullptr
        );
        pool.push_back({surf, {w, h}, true});
        return surf;
    }

    void release(sk_sp<SkSurface> surf) {
        for (auto& ps : pool) {
            if (ps.surface == surf) { ps.inUse = false; return; }
        }
    }
};
```

---

## 8. Stroke Rendering

### Overview

Strokes are the primary content type. A stroke is produced by a series of input events (touch/stylus points) which are fitted to a smooth curve and then rasterized into a **variable-width filled path**.

### Step 1: Raw Points to Bezier Curve Fitting

Raw input points are noisy (finger tremor, quantization, jitter). Direct polyline rendering produces jagged strokes. OpenBoard fits a **piecewise cubic Bezier curve** to the raw points using the **Schneider algorithm** ("An Algorithm for Automatically Fitting Digitized Curves", Graphics Gems I).

```cpp
struct BezierSegment {
    SkPoint p0, p1, p2, p3;  // Cubic Bezier: p0=start, p1,p2=control, p3=end
};

std::vector<BezierSegment> fitCurve(
    const std::vector<InputPoint>& pts,
    float maxError = 2.0f       // Max allowable fitting error in world units
) {
    if (pts.size() < 2) return {};

    // Degenerate case: two points -> degenerate cubic (straight line)
    if (pts.size() == 2) {
        SkPoint d = (pts[1].pos - pts[0].pos) * (1.0f / 3.0f);
        return {{ pts[0].pos, pts[0].pos + d, pts[1].pos - d, pts[1].pos }};
    }

    // Chord-length parameterization: t_i proportional to arc length
    std::vector<float> u = chordLengthParameterize(pts);

    // Recursive Schneider fitting
    return fitCubicRecursive(
        pts, u, 0, (int)pts.size() - 1,
        leftTangent(pts),     // Tangent at start
        rightTangent(pts),    // Tangent at end
        maxError
    );
}
```

### Step 2: Variable-Width Outline Path

The stroke is constructed as a **closed filled outline path** rather than a stroked center-line. This is essential because:

1. Pressure modulates the width at each point — a stroked path cannot represent this.
2. Filled paths render faster than stroked paths with complex caps/joins.
3. The outline path can be cached as-is after fitting.

```cpp
SkPath buildVariableWidthPath(
    const std::vector<BezierSegment>& segs,
    const std::vector<float>& widths     // width sample at each Bezier parameter
) {
    SkPath leftSide, rightSide;

    for (const auto& seg : segs) {
        const int SUBDIVISIONS = 16;     // Samples per Bezier segment
        for (int i = 0; i <= SUBDIVISIONS; i++) {
            float t = (float)i / SUBDIVISIONS;

            SkPoint center  = evalCubicBezier(seg, t);
            SkPoint tangent = evalCubicBezierTangent(seg, t);
            tangent.normalize();

            // Normal = perpendicular to tangent
            SkPoint normal = SkPoint::Make(-tangent.y(), tangent.x());

            float halfWidth = interpolateWidth(widths, seg, t) / 2.0f;

            leftSide.lineTo(center + normal * halfWidth);
            rightSide.lineTo(center - normal * halfWidth);
        }
    }

    // Construct closed outline: left side forward, right side reversed
    SkPath outline = leftSide;
    SkPath reversedRight;
    rightSide.reverseAddPath(&reversedRight);
    outline.addPath(reversedRight);
    outline.close();

    return outline;
}
```

### Step 3: Pressure to Width Mapping

```cpp
float pressureToWidth(float normalizedPressure, const BrushConfig& cfg) {
    // Sub-linear power curve for perceptual pressure linearity.
    // Exponent 0.6: light touch gives moderate width, heavy touch maxes out smoothly.
    float p     = std::clamp(normalizedPressure, 0.0f, 1.0f);
    float eased = std::pow(p, cfg.pressureCurveExponent);   // default: 0.6
    return cfg.minWidth + eased * (cfg.maxWidth - cfg.minWidth);
}
```

### Step 4: Render the Stroke

```cpp
void Stroke::draw(SkCanvas* canvas) const {
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);   // Fill the outline path
    paint.setColor4f(color);
    paint.setAntiAlias(true);

    canvas->drawPath(outlinePath, paint);   // Single draw call per stroke

    // Draw rounded end caps (circles at start/end)
    if (endCap == EndCap::Round) {
        drawRoundCap(canvas, startPoint, startRadius, paint);
        drawRoundCap(canvas, endPoint,   endRadius,   paint);
    }
}
```

### Incremental Rendering During Active Input

While the user is drawing, new points arrive at ~120 Hz. Only the **new tail segment** needs to be fitted and appended — not the entire stroke:

```cpp
void StrokeBuilder::addPoint(const InputPoint& pt) {
    rawPoints.push_back(pt);
    if (rawPoints.size() < 4) return;    // Need minimum 4 pts for cubic

    // Fit only a sliding window of the most recent points for speed
    int windowStart = std::max(0, (int)rawPoints.size() - FITTING_WINDOW_SIZE);
    auto window = std::vector<InputPoint>(
        rawPoints.begin() + windowStart, rawPoints.end());

    auto newSegments = fitCurve(window, /*maxError=*/3.0f);

    for (auto& seg : newSegments) {
        accumulatedPath.cubicTo(seg.p1, seg.p2, seg.p3);
    }

    // Track dirty rect for partial redraw
    dirtyRect.join(computeSegmentBBox(newSegments.back(), currentWidth));
}
```

---

## 9. Anti-Aliasing Strategy

### Multi-Layer Approach

OpenBoard uses a combination of anti-aliasing techniques:

1. **4x MSAA** on all layer SkSurfaces — handles geometry edge aliasing (path outlines, shape edges).
2. **Skia analytical AA** (`SkPaint::setAntiAlias(true)`) — sub-pixel coverage computation for path rendering, independent of MSAA.
3. **No FXAA / SMAA / TAA** — post-process AA algorithms blur fine ink strokes, calligraphic details, and small text. They are explicitly rejected.

### MSAA Configuration

```cpp
// Determine maximum supported MSAA level
int maxSamples =
    grCtx->maxSurfaceSampleCountForColorType(kRGBA_8888_SkColorType);
int sampleCount = std::min(4, maxSamples);  // Request 4x, clamp to HW max

// All layer surfaces use MSAA
auto layerSurf = SkSurface::MakeRenderTarget(
    grCtx, SkBudgeted::kYes,
    SkImageInfo::MakeN32Premul(w, h),
    sampleCount,
    kTopLeft_GrSurfaceOrigin,
    &surfaceProps
);
```

When the on-screen surface (wrapping the EGL default framebuffer) does not support MSAA (older Mali GPUs), OpenBoard falls back to rendering to an MSAA offscreen surface and resolving it to the framebuffer:

```cpp
if (!deviceSupportsMSAAFramebuffer()) {
    // Render to MSAA offscreen -> resolve -> blit to framebuffer
    renderToMSAAOffscreen();
    blitResolvedToFramebuffer();
} else {
    renderDirectlyToMSAAFramebuffer();
}
```

### LCD Sub-Pixel AA for Text

On most Android devices, the display uses an RGB horizontal sub-pixel layout. OpenBoard configures text rendering to exploit this:

```cpp
SkSurfaceProps props(
    SkSurfaceProps::kUseDeviceIndependentFonts_Flag,
    kRGB_H_SkPixelGeometry      // Horizontal RGB stripe sub-pixels
);
```

On screens with unknown or vertical sub-pixel layout, the engine falls back to grayscale AA to avoid color fringing.

---

## 10. Caching: SkPicture and SkImage

### Cache Hierarchy

```
L1: SkPicture cache    -- per-layer recorded draw commands       (CPU memory, ~1-10 MB)
L2: SkImage tile cache -- rasterized tile images                 (GPU VRAM,  ~128-256 MB)
L3: Disk tile cache    -- compressed tiles for huge documents    (Disk, optional, ~500 MB+)
```

### SkPicture Cache

An `SkPicture` records all draw calls for a layer into a compact binary representation without executing them on the GPU. This is used to cache draw commands for **unmodified layers**, allowing fast replay without re-traversing the object graph:

```cpp
// Record all draw calls for a layer into an SkPicture
sk_sp<SkPicture> recordLayer(const LayerSnapshot& layer, const SkRect& bounds) {
    SkPictureRecorder recorder;
    SkCanvas* c = recorder.beginRecording(bounds);
    for (auto& drawable : layer.drawables) {
        drawable->draw(c);          // Records commands, does NOT execute on GPU
    }
    return recorder.finishRecordingAsPicture();
}

// Replay a cached picture onto any canvas
void drawCachedLayer(SkCanvas* canvas, sk_sp<SkPicture> picture) {
    canvas->drawPicture(picture);   // Replays all recorded commands
}

// Invalidate when layer content changes (new stroke, erase, etc.)
void LayerCache::invalidate(LayerID id) {
    pictureCache.erase(id);
}
```

### SkImage GPU Tile Cache

Rendered tiles are stored as `SkImage` objects backed by GPU textures. Re-drawing a cached tile is a single textured-quad draw call (very cheap):

```cpp
struct TileCacheEntry {
    sk_sp<SkImage> gpuImage;            // Lives in GPU VRAM
    uint64_t       contentVersion;      // Incremented on any content change
    std::chrono::steady_clock::time_point lastAccess;
};

// LRU eviction when cache exceeds memory budget
void TileCache::evictLRU() {
    while (estimatedMemoryUsageMB() > MAX_TILE_MEMORY_MB) {
        auto oldest = std::min_element(cache.begin(), cache.end(),
            [](const auto& a, const auto& b) {
                return a.second.lastAccess < b.second.lastAccess;
            });
        if (oldest != cache.end()) cache.erase(oldest);
    }
}
```

### Cache Invalidation on Edit

When the user draws, erases, or transforms objects, the cache must be invalidated for the affected region:

```cpp
void onStrokeCommitted(const Stroke& stroke) {
    SkRect bbox = computeStrokeBBox(stroke);
    tileCache.invalidateRect(bbox);      // Invalidate overlapping tiles
    layerCache.invalidate(stroke.layerID); // Invalidate SkPicture for this layer
}
```

---

## 11. Frame Timing

### Target Frame Rates

| Display Type | Target FPS | Frame Budget |
|---|---|---|
| Standard (60Hz) | 60 fps | 16.67 ms |
| High-refresh (90Hz) | 90 fps | 11.11 ms |
| ProMotion (120Hz) | 120 fps | 8.33 ms |
| ProMotion + VRR | Adaptive | 8.33-50 ms |

OpenBoard signals the preferred frame rate to Android's display pipeline:

```cpp
// Request adaptive 120Hz refresh on ProMotion displays
ANativeWindow_setFrameRate(
    nativeWindow,
    120.0f,
    ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE
);
```

### Frame Budget Breakdown (16.67ms @ 60fps target)

| Phase | Typical Cost | Notes |
|---|---|---|
| Snapshot capture | ~0.05-0.1 ms | Lock-free atomic read |
| R-Tree spatial query | ~0.2-0.5 ms | Depends on tree depth |
| Skia draw calls (CPU) | ~2-6 ms | Encode commands into GPU command buffer |
| GPU rasterization | ~4-9 ms | Runs in parallel with next CPU frame |
| grContext flush+submit | ~0.5-1 ms | Kick off GPU work |
| eglSwapBuffers | ~0.5-2 ms | VSync wait + buffer swap |
| **Total typical** | **~8-14 ms** | Leaves headroom for complex canvases |

### Frame Profiler

```cpp
class FrameProfiler {
    static constexpr int HISTORY = 120;   // 2 seconds at 60fps
    std::array<float, HISTORY> frameTimes{};
    int head = 0;

public:
    void recordFrame(float dtMs) {
        frameTimes[head++ % HISTORY] = dtMs;
    }

    float averageFps() const {
        float sum = 0.0f;
        for (float t : frameTimes) sum += t;
        return 1000.0f / (sum / HISTORY);
    }

    float p95FrameTime() const {
        std::array<float, HISTORY> sorted = frameTimes;
        std::sort(sorted.begin(), sorted.end());
        return sorted[(int)(HISTORY * 0.95f)];
    }
};
```

### Adaptive Quality Fallback

When frame times consistently exceed the budget (complex canvas, slow GPU), OpenBoard automatically reduces rendering quality to maintain fluidity:

```cpp
RenderQuality adaptQuality(const FrameProfiler& profiler, float targetFps) {
    float fps = profiler.averageFps();
    if (fps >= targetFps * 0.95f) return RenderQuality::High;    // All features
    if (fps >= targetFps * 0.75f) return RenderQuality::Medium;  // Reduce MSAA
    return RenderQuality::Draft;    // No MSAA, linear bezier, larger tiles
}
```

---

## 12. Zoom Level and Rendering Quality

### Resolution-Independent Rendering

All canvas content is stored in **world units** (WU) — device-independent coordinates. The camera matrix maps world units to screen pixels. Strokes look equally sharp at any zoom level because the geometry itself scales with the camera, not the rasterizer.

```
World Space (WU) --[Camera Matrix]--> Screen Space (px)

Camera Matrix = Scale(zoom) * Translate(-panX, -panY)

At zoom=1.0: 1 WU = 1 screen pixel
At zoom=2.0: 1 WU = 2 screen pixels (everything appears 2x larger)
At zoom=0.1: 1 WU = 0.1 screen pixels (everything appears 10x smaller)
```

Because strokes are stored as world-unit paths and transformed by the camera matrix at render time, there is **no quality degradation from zooming** — unlike bitmap-based zoom.

### Zoom-Dependent Rendering Detail

| Zoom Range | Tile LOD | Stroke Rendering | Text Rendering |
|---|---|---|---|
| > 400% | LOD 0 | 32-sample subdivision | Full LCD hinting |
| 100%-400% | LOD 0 | 16-sample subdivision | Full hinting |
| 50%-100% | LOD 0 | Full Bezier | Full hinting |
| 25%-50% | LOD 1 | Simplified (8-sample) | Reduced hinting |
| < 25% | LOD 2 | Polyline fallback | Bitmap glyph cache |

### Curve Subdivision Count

At very high zoom, stroke curves must be subdivided more finely to remain smooth at screen resolution:

```cpp
int curveSubdivisions(float zoom) {
    if (zoom > 4.0f) return 32;    // Very zoomed in — need fine subdivision
    if (zoom > 2.0f) return 24;
    if (zoom > 1.0f) return 16;    // Normal
    return 8;                       // Zoomed out — coarse is fine
}
```

### Pixel-Perfect Stroke Width

Since strokes are stored as world-unit outline paths, the camera transform automatically scales the outline to the correct screen width. A 2 WU wide stroke at zoom=3.0 appears as 6 screen pixels wide — no special handling required.

---

## 13. Thumbnail Generation

Thumbnails are used for the **file browser preview cards**, the **page list sidebar** in multi-page mode, and the **share sheet image** when the user shares a document.

### Thumbnail Size Targets

| Use Case | Size | Format | Quality |
|---|---|---|---|
| File browser card | 320 x 240 px | JPEG | q=80 |
| Page list sidebar | 120 x 90 px | PNG | Lossless |
| Share image | 1280 x 960 px | JPEG | q=90 |
| iOS/Android widget | 512 x 512 px | PNG | Lossless |

### Thumbnail Render Process

Thumbnails use **CPU rasterization** (`SkSurface::MakeRaster`) — no EGL context or GPU required. This allows thumbnail generation on any thread without GPU context management:

```cpp
sk_sp<SkData> generateThumbnail(
    const Document& doc,
    PageIndex page,
    int targetW,
    int targetH
) {
    // Compute scale-to-fit
    SkSize pageSize = doc.getPage(page).sizeWU();
    float scaleX    = (float)targetW / pageSize.width();
    float scaleY    = (float)targetH / pageSize.height();
    float scale     = std::min(scaleX, scaleY);    // Preserve aspect ratio

    int renderW = (int)(pageSize.width()  * scale);
    int renderH = (int)(pageSize.height() * scale);

    // CPU raster surface — no GPU context needed
    auto info = SkImageInfo::MakeN32Premul(renderW, renderH);
    auto surf = SkSurface::MakeRaster(info);
    SkCanvas* c = surf->getCanvas();
    c->clear(SK_ColorWHITE);
    c->scale(scale, scale);

    // Use Draft quality: skip MSAA, use simplified strokes
    renderPage(c, doc, page, RenderQuality::Draft);

    return surf->makeImageSnapshot()
               ->encodeToData(SkEncodedImageFormat::kJPEG, 80);
}
```

### Background Thumbnail Generation

Thumbnails are generated on a background thread pool to avoid blocking the main thread or render thread:

```cpp
void ThumbnailManager::requestThumbnail(PageIndex page, ThumbnailSize size) {
    ThumbnailKey key{page, size};
    if (pendingRequests.count(key)) return;    // Already in queue

    pendingRequests.insert(key);
    thumbnailPool.enqueue([this, page, size, key] {
        auto [w, h] = thumbnailDimensions(size);
        auto data   = generateThumbnail(*document, page, w, h);

        std::lock_guard lock(cacheMutex);
        thumbnailCache[key] = SkImage::MakeFromEncoded(data);
        pendingRequests.erase(key);

        // Notify UI thread to update page thumbnail widget
        mainThreadHandler.post([this, page] { onThumbnailReady(page); });
    });
}
```

---

## 14. Screenshot / Export Pipeline

### Supported Export Formats

| Format | DPI Options | Vector/Raster | Primary Use |
|---|---|---|---|
| PNG | 72 / 150 / 300 / 600 dpi | Raster | Web, lossless archival |
| JPEG | 72 / 150 / 300 dpi | Raster | Social sharing, email |
| PDF | N/A (vector) | Vector | Print, professional archival |
| SVG | N/A (vector) | Vector | Web embedding, Figma import |

### Raster Export

World units are defined so that **1 WU = 1/96 inch** at the baseline 96 dpi resolution. Exporting at 300 dpi means multiplying all coordinates by 300/96 = 3.125x.

```cpp
sk_sp<SkData> exportPageRaster(
    const Document&      doc,
    PageIndex            page,
    int                  dpi,
    SkEncodedImageFormat format,
    int                  quality = 90
) {
    SkSize pageSize = doc.getPage(page).sizeWU();
    constexpr float BASE_DPI = 96.0f;
    float scale = (float)dpi / BASE_DPI;       // e.g., 300/96 = 3.125

    int pixelW = (int)std::ceil(pageSize.width()  * scale);
    int pixelH = (int)std::ceil(pageSize.height() * scale);

    // sRGB color space for accurate color reproduction
    auto colorSpace = SkColorSpace::MakeSRGB();
    auto info = SkImageInfo::Make(
        pixelW, pixelH,
        kRGBA_8888_SkColorType, kPremul_SkAlphaType,
        colorSpace
    );

    // CPU surface — allows export on background thread, any size
    auto surf = SkSurface::MakeRaster(info);
    SkCanvas* c = surf->getCanvas();
    c->clear(SK_ColorWHITE);    // White background
    c->scale(scale, scale);

    // High quality: full Bezier subdivision, no LOD shortcuts
    renderPage(c, doc, page, RenderQuality::Export);

    return surf->makeImageSnapshot()->encodeToData(format, quality);
}
```

### PDF Export

PDF export uses Skia's `SkPDFDocument` API. Vector content (strokes as paths, text as glyphs) is kept as PDF vector operators — it does not get rasterized. Only embedded raster images are stored as compressed bitmaps.

```cpp
sk_sp<SkData> exportPDF(const Document& doc) {
    auto stream   = SkDynamicMemoryWStream();
    auto metadata = SkPDF::Metadata();
    metadata.fTitle    = std::string(doc.title());
    metadata.fAuthor   = std::string(doc.authorName());
    metadata.fCreator  = "OpenBoard Engine v1.0";
    metadata.fRasterDPI = 300;    // DPI for any embedded raster images

    auto pdfDoc = SkPDF::MakeDocument(&stream, metadata);

    for (PageIndex p = 0; p < doc.pageCount(); p++) {
        SkSize    sz     = doc.getPage(p).sizeWU();
        SkCanvas* canvas = pdfDoc->beginPage(sz.width(), sz.height());

        // Draw page content into the PDF canvas
        // Skia translates SkCanvas calls to PDF operators
        renderPage(canvas, doc, p, RenderQuality::Export);

        pdfDoc->endPage();
    }
    pdfDoc->close();

    return stream.detachAsData();
}
```

### Progress Reporting for Multi-Page Export

```cpp
class ExportJob {
public:
    std::atomic<int>   completedPages{0};
    std::atomic<int>   totalPages{0};
    std::atomic<bool>  cancelled{false};
    std::atomic<bool>  failed{false};
    std::string        errorMessage;

    float progress() const {
        int total = totalPages.load();
        return total > 0 ? (float)completedPages.load() / total : 0.0f;
    }

    void run(const Document& doc, const ExportOptions& opts) {
        totalPages = doc.pageCount();
        for (int p = 0; p < doc.pageCount(); p++) {
            if (cancelled) break;
            try {
                exportPage(doc, p, opts);
                completedPages++;
            } catch (const std::exception& e) {
                errorMessage = e.what();
                failed = true;
                break;
            }
        }
    }
};
```

---

## 15. SkPaint Configuration per Tool

Each drawing tool configures `SkPaint` differently to achieve its characteristic visual effect.

### Pen Tool

The pen uses a variable-width filled path. The SkPaint is simple — all the visual work is in the path geometry.

```cpp
SkPaint penPaint(SkColor4f color) {
    SkPaint p;
    p.setStyle(SkPaint::kFill_Style);       // Fill the pre-built outline path
    p.setAntiAlias(true);
    p.setColor4f(color, nullptr);
    p.setBlendMode(SkBlendMode::kSrcOver);  // Standard alpha compositing
    return p;
}
```

### Pencil Tool

The pencil simulates graphite with a textured, semi-transparent appearance that varies with pressure.

```cpp
SkPaint pencilPaint(float normalizedPressure, SkColor userColor,
                     sk_sp<SkShader> grainShader)
{
    SkPaint p;
    p.setStyle(SkPaint::kFill_Style);
    p.setAntiAlias(true);

    // Low pressure = faint, high pressure = darker. Non-linear response.
    float alphaFraction = 0.3f + 0.7f * std::pow(normalizedPressure, 0.7f);
    uint8_t alpha = (uint8_t)(255 * alphaFraction);
    p.setColor(SkColorSetA(userColor, alpha));

    // Dithering reduces color banding in low-alpha regions
    p.setDither(true);

    // Grain texture shader for pencil texture (tiled Perlin-noise texture)
    p.setShader(grainShader);
    return p;
}
```

### Highlighter Tool

The highlighter uses a fixed-width stroke with square caps (for flat start/end), multiply blend mode to preserve text below, and no anti-aliasing for crisp edges.

```cpp
SkPaint highlighterPaint(SkColor color, float widthWU) {
    SkPaint p;
    p.setStyle(SkPaint::kStroke_Style);     // Stroke the center-line (no variable width)
    p.setStrokeWidth(widthWU);
    p.setStrokeCap(SkPaint::kSquare_Cap);   // Flat start/end (like a real highlighter)
    p.setStrokeJoin(SkPaint::kBevel_Join);
    p.setAntiAlias(false);                  // Hard pixel-aligned edges (intentional)
    p.setColor(SkColorSetA(color, 120));    // ~47% opacity
    p.setBlendMode(SkBlendMode::kMultiply); // Multiplied: darkens without covering text
    return p;
}
```

### Eraser Tool

The eraser punches through the current layer using `DstOut` blend mode, making pixels transparent. On layers with a white background, use `SrcOver` with white color instead.

```cpp
SkPaint eraserPaint(bool layerHasTransparentBackground) {
    SkPaint p;
    p.setStyle(SkPaint::kFill_Style);
    p.setAntiAlias(true);

    if (layerHasTransparentBackground) {
        // DstOut: makes destination pixels transparent wherever source is opaque
        p.setColor(SK_ColorWHITE);          // Color doesn't matter for DstOut
        p.setBlendMode(SkBlendMode::kDstOut);
    } else {
        // Paint white on opaque background layers
        p.setColor(SK_ColorWHITE);
        p.setBlendMode(SkBlendMode::kSrcOver);
    }
    return p;
}
```

### Marker / Brush Tool

The marker simulates a bristle brush with a soft-edged filled path and optional per-bristle detail via an SkShader.

```cpp
SkPaint markerPaint(sk_sp<SkShader> bristleShader, float softness) {
    SkPaint p;
    p.setStyle(SkPaint::kFill_Style);
    p.setAntiAlias(true);

    if (bristleShader) {
        p.setShader(bristleShader);        // Per-bristle gradient pattern
    }
    p.setBlendMode(SkBlendMode::kSrcOver);

    if (softness > 0.0f) {
        // Gaussian blur mask filter for soft brush edges
        // sigma in screen pixels; convert from world units at render time
        float sigma = softness * 0.3f;
        p.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
    }
    return p;
}
```

### Shape Tool (Rectangle, Ellipse, Line)

```cpp
SkPaint shapeStrokePaint(SkColor color, float strokeWidthWU) {
    SkPaint p;
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(strokeWidthWU);
    p.setStrokeCap(SkPaint::kRound_Cap);    // Rounded line endpoints
    p.setStrokeJoin(SkPaint::kRound_Join);  // Rounded corners
    p.setAntiAlias(true);
    p.setColor(color);
    p.setBlendMode(SkBlendMode::kSrcOver);
    return p;
}

SkPaint shapeFillPaint(SkColor fillColor) {
    SkPaint p;
    p.setStyle(SkPaint::kFill_Style);
    p.setAntiAlias(true);
    p.setColor(fillColor);
    p.setBlendMode(SkBlendMode::kSrcOver);
    return p;
}
```

### Tool Paint Summary Table

| Tool | Style | Blend Mode | Anti-Alias | Width Control | Special Properties |
|---|---|---|---|---|---|
| Pen | Fill | SrcOver | Yes | Variable (pressure) | Bezier outline path |
| Pencil | Fill | SrcOver | Yes | Variable (pressure) | Grain shader, dither, alpha varies |
| Highlighter | Stroke | Multiply | No | Fixed | Square cap, semi-transparent |
| Eraser (transparent layer) | Fill | DstOut | Yes | Variable (pressure) | Punches alpha hole |
| Eraser (opaque layer) | Fill | SrcOver | Yes | Variable (pressure) | Paints white |
| Marker/Brush | Fill | SrcOver | Yes | Variable (pressure) | Bristle shader, blur mask |
| Shape (stroke) | Stroke | SrcOver | Yes | Fixed | Round cap and join |
| Shape (fill) | Fill | SrcOver | Yes | N/A | Solid color |
| Text | N/A | SrcOver | Yes (LCD) | N/A | SkTextBlob, font rasterizer |

---

*End of 03_RENDERING_PIPELINE.md*
