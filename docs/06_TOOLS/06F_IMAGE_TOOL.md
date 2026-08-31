# 06F — Image Tool

> **Module**: `tools/image/`  
> **Primary classes**: `ImageTool`, `ImageElement`, `ImageDecoder`, `TextureCache`  
> **Dependencies**: `stb_image`, Skia (`SkImage`, `SkCanvas`, `SkColorFilter`), GPU texture manager, LayerManager

---

## Table of Contents

1. [Supported Import Formats](#1-supported-import-formats)
2. [Import Flow](#2-import-flow)
3. [ImageElement Model](#3-imageelement-model)
4. [Image Placement](#4-image-placement)
5. [Resize Handles](#5-resize-handles)
6. [Non-Destructive Crop Tool](#6-non-destructive-crop-tool)
7. [Image Filters](#7-image-filters)
8. [Image as Background vs Canvas Object](#8-image-as-background-vs-canvas-object)
9. [Pasting Images from Clipboard](#9-pasting-images-from-clipboard)
10. [Memory Management — GPU Texture Atlas and LRU Cache](#10-memory-management--gpu-texture-atlas-and-lru-cache)
11. [Lazy Loading for Large Files](#11-lazy-loading-for-large-files)
12. [ImageTool API Reference](#12-imagetool-api-reference)

---

## 1. Supported Import Formats

| Format | Extension | Notes |
|---|---|---|
| PNG | `.png` | Full RGBA, transparency supported |
| JPEG | `.jpg`, `.jpeg` | No transparency; decoded to RGB |
| WebP | `.webp` | Lossy and lossless; transparency supported |
| GIF | `.gif` | Static first frame only (animation not rendered) |
| BMP | `.bmp` | Legacy; no transparency |
| TIFF | `.tif`, `.tiff` | Multi-page: first page imported |
| SVG | `.svg` | Rasterized at import resolution (vector SVG shapes use ShapeTool) |

### 1.1 Format Detection

Format is detected by file magic bytes, not file extension (extensions may be wrong):

```cpp
ImageFormat ImageDecoder::detectFormat(const uint8_t* data, size_t size) {
    if (size >= 8 && data[0]==0x89 && data[1]=='P' && data[2]=='N' && data[3]=='G')
        return ImageFormat::PNG;
    if (size >= 3 && data[0]==0xFF && data[1]==0xD8 && data[2]==0xFF)
        return ImageFormat::JPEG;
    if (size >= 4 && data[0]=='R' && data[1]=='I' && data[2]=='F' && data[3]=='F')
        return ImageFormat::WebP;
    if (size >= 6 && data[0]=='G' && data[1]=='I' && data[2]=='F')
        return ImageFormat::GIF;
    if (size >= 2 && data[0]=='B' && data[1]=='M')
        return ImageFormat::BMP;
    if (size >= 4 && data[0]=='I' && data[1]=='I' && data[2]==42)
        return ImageFormat::TIFF;
    return ImageFormat::Unknown;
}
```

---

## 2. Import Flow

### 2.1 Complete Pipeline

```
User triggers image import (file picker / drag-drop / clipboard paste)
         |
         v
File loaded into memory buffer (std::vector<uint8_t>)
         |
         v
ImageDecoder::detectFormat()
         |
         v
stb_image::stbi_load_from_memory()
   -> decoded to RGBA8 pixel buffer
   -> width, height, channels
         |
         v
SkBitmap created from pixel buffer
   -> SkColorType::kRGBA_8888_SkColorType
   -> SkAlphaType::kPremul_SkAlphaType
         |
         v
SkImage::MakeFromBitmap() -> CPU-side SkImage
         |
         v
SkImage::makeTextureImage(grContext) -> GPU texture
         |
         v
TextureCache::insert(imageId, gpuImage)
         |
         v
ImageElement created with textureHandle = imageId
LayerManager::addElement(layerId, imageElem)
DrawImageCommand pushed to UndoStack
         |
         v
Image placed at canvas center at natural size
Image enters selection/handle mode immediately
```

### 2.2 stb_image Decode

```cpp
// tools/image/ImageDecoder.cpp
struct DecodedImage {
    std::vector<uint8_t> pixels;
    int                  width, height, channels;
};

DecodedImage ImageDecoder::decode(const uint8_t* fileData, size_t fileSize) {
    DecodedImage result;
    stbi_uc* raw = stbi_load_from_memory(
        fileData, static_cast<int>(fileSize),
        &result.width, &result.height, &result.channels,
        STBI_rgb_alpha);   // force 4 channels output

    if (!raw) {
        throw ImageDecodeException(stbi_failure_reason());
    }

    size_t totalBytes = result.width * result.height * 4;
    result.pixels.assign(raw, raw + totalBytes);
    stbi_image_free(raw);
    result.channels = 4;
    return result;
}
```

### 2.3 GPU Upload

```cpp
sk_sp<SkImage> ImageDecoder::uploadToGPU(const DecodedImage& img,
                                          GrDirectContext* grCtx)
{
    SkImageInfo info = SkImageInfo::Make(img.width, img.height,
                                         kRGBA_8888_SkColorType,
                                         kPremul_SkAlphaType);
    SkBitmap bmp;
    bmp.installPixels(info, (void*)img.pixels.data(), img.width * 4);
    bmp.setImmutable();

    auto cpuImage  = SkImage::MakeFromBitmap(bmp);
    auto gpuImage  = cpuImage->makeTextureImage(grCtx, GrMipmapped::kYes);
    return gpuImage;
}
```

Mipmaps are generated at upload time for high-quality downscaling at low zoom levels.

---

## 3. ImageElement Model

```cpp
// model/ImageElement.h
struct ImageElement : CanvasElement {
    // Texture handle
    ImageId       textureId;         // key into TextureCache
    sk_sp<SkImage> image;            // GPU texture reference (may be null if evicted)

    // Source and destination rectangles
    SkRect        srcRect;           // region of original image to display
                                     // (modified by crop; initially full image)
    SkRect        dstRect;           // position/size on canvas (in canvas pixels)

    // Natural dimensions (original file dimensions in pixels)
    int           naturalWidth;
    int           naturalHeight;

    // Transform
    float         rotationDeg;       // rotation around dstRect center
    bool          flipH;             // horizontal flip
    bool          flipV;             // vertical flip

    // Visual properties
    float         opacity;           // [0.0, 1.0]

    // Layer role
    ImageRole     role;              // CanvasObject or BackgroundImage

    // Filter settings
    bool          filterGrayscale;
    float         filterBrightness;  // [-1.0, 1.0]; 0 = no change
    float         filterContrast;    // [-1.0, 1.0]; 0 = no change
    float         filterSaturation;  // [-1.0, 1.0]; 0 = no change

    // CanvasElement interface
    SkRect      boundingBox() const override { return dstRect; }
    void        applyTransform(const SkMatrix&) override;
    ElementType type() const override { return ElementType::Image; }
};

enum class ImageRole {
    CanvasObject,     // placed on canvas like any other element
    BackgroundImage   // fills the entire page behind all layers
};
```

---

## 4. Image Placement

### 4.1 Default Placement at Natural Size

After import, the image is placed at the canvas center at its natural pixel size, unless the image is larger than the current viewport (in which case it is scaled to fit 80% of the viewport):

```cpp
SkRect ImageTool::computeInitialDstRect(int naturalW, int naturalH) const {
    SkRect viewport = m_viewport->canvasBounds();
    float maxW = viewport.width()  * 0.8f;
    float maxH = viewport.height() * 0.8f;

    float scale = std::min({ 1.0f,
                              maxW / naturalW,
                              maxH / naturalH });
    float w = naturalW * scale;
    float h = naturalH * scale;
    return SkRect::MakeXYWH(viewport.centerX() - w * 0.5f,
                             viewport.centerY() - h * 0.5f, w, h);
}
```

### 4.2 Drag-and-Drop Placement

On desktop, images can be dropped onto the canvas via OS drag-and-drop. The drop position is used as the image center:

```cpp
void ImageTool::onDropEvent(const DropEvent& e) {
    auto decoded = ImageDecoder::decode(e.fileData, e.fileSize);
    auto gpuImg  = ImageDecoder::uploadToGPU(decoded, m_grContext);
    ImageId imgId = m_textureCache->insert(gpuImg);

    ImageElement elem;
    elem.textureId    = imgId;
    elem.image        = gpuImg;
    elem.naturalWidth  = decoded.width;
    elem.naturalHeight = decoded.height;
    elem.srcRect      = SkRect::MakeWH(decoded.width, decoded.height);
    elem.dstRect      = computeInitialDstRect(decoded.width, decoded.height);
    // Center on drop position
    SkVector offset = { e.canvasX - elem.dstRect.centerX(),
                        e.canvasY - elem.dstRect.centerY() };
    elem.dstRect.offset(offset);

    m_layerManager->addElement(m_activeLayerId, elem);
    m_undoStack->push(std::make_unique<DrawImageCommand>(
        m_layerManager, m_activeLayerId, elem));
}
```

---

## 5. Resize Handles

### 5.1 Handle Behavior

The 8-handle layout (same as SelectionTool) is displayed around the `dstRect`. Dragging handles updates `dstRect`:

- **Corner handles**: Scale the image, maintaining aspect ratio by default.
- **Edge handles**: Non-uniform scale (stretch in one axis).
- **Shift modifier**: Force uniform scale from center (when held during corner drag).
- **Rotate handle**: Rotate the image; stored as `rotationDeg`.

### 5.2 Aspect Ratio Lock

```cpp
void ImageTool::onCornerHandleDrag(HandleType h, SkPoint pos, bool shiftKey) {
    SkRect& r    = m_editElem->dstRect;
    float aspect = (float)m_editElem->naturalWidth / m_editElem->naturalHeight;

    // Compute new corner position
    SkPoint anchor = oppositeCorner(h, r);
    float newW = std::abs(pos.x() - anchor.x());
    float newH = std::abs(pos.y() - anchor.y());

    if (!shiftKey) {
        // Lock aspect ratio: constrain to whichever dimension moved more
        float scaleByW = newW / r.width();
        float scaleByH = newH / r.height();
        float scale    = std::max(scaleByW, scaleByH);
        newW = r.width()  * scale;
        newH = r.height() * scale;
    }

    // Rebuild dstRect from anchor + new size
    r = SkRect::MakeXYWH(anchor.x(), anchor.y(), newW, newH);
    normalizeRect(r);   // ensure left < right, top < bottom
}
```

### 5.3 Flip Controls

Horizontal and vertical flip are applied as SkMatrix transforms during rendering:

```cpp
SkMatrix ImageElement::buildRenderMatrix() const {
    SkMatrix m = SkMatrix::I();
    if (flipH) m.preScale(-1.0f, 1.0f, dstRect.centerX(), dstRect.centerY());
    if (flipV) m.preScale(1.0f, -1.0f, dstRect.centerX(), dstRect.centerY());
    if (rotationDeg != 0.0f)
        m.preRotate(rotationDeg, dstRect.centerX(), dstRect.centerY());
    return m;
}
```

---

## 6. Non-Destructive Crop Tool

### 6.1 Concept

Cropping is non-destructive: it only adjusts `srcRect` (which sub-region of the original image to display) without discarding any pixel data. The full original image remains in the GPU texture. The crop can be undone or reset at any time.

```cpp
// Before crop:
//   srcRect = (0, 0, naturalWidth, naturalHeight)  -- entire image
//   dstRect = (cx, cy, displayW, displayH)

// After crop to right 50%:
//   srcRect = (naturalWidth/2, 0, naturalWidth, naturalHeight)
//   dstRect stays the same (display size unchanged)
```

### 6.2 Crop UI

When the crop tool is activated on an image element:

1. The full original image is revealed (shown lighter, outside the current crop area).
2. A darker overlay shows the cropped region.
3. The user drags the crop rectangle edges to adjust.
4. Pressing Enter confirms; Escape cancels.

```cpp
void CropTool::onPointerMove(const PointerEvent& e) {
    // m_cropRect is in image-local coordinates [0, naturalW] x [0, naturalH]
    if (m_draggingEdge == Edge::Right) {
        m_cropRect.fRight = std::clamp(imageLocalX(e.x),
                                        m_cropRect.fLeft + MIN_CROP_SIZE,
                                        (float)m_editElem->naturalWidth);
    }
    // ... similar for other edges and corners
    rebuildCropOverlay();
}
```

### 6.3 Applying the Crop

```cpp
void CropTool::commitCrop() {
    SkRect oldSrc = m_editElem->srcRect;
    SkRect newSrc = m_cropRect;  // in image pixel coords

    // Scale dstRect to maintain display density (optional behavior)
    float scaleX = m_editElem->dstRect.width()  / oldSrc.width();
    float scaleY = m_editElem->dstRect.height() / oldSrc.height();
    SkRect newDst = SkRect::MakeXYWH(
        m_editElem->dstRect.fLeft,
        m_editElem->dstRect.fTop,
        newSrc.width()  * scaleX,
        newSrc.height() * scaleY);

    auto cmd = std::make_unique<CropImageCommand>(
        m_layerManager, m_editElem->id,
        oldSrc, m_editElem->dstRect,
        newSrc, newDst);
    m_undoStack->execute(std::move(cmd));
}
```

---

## 7. Image Filters

### 7.1 Available Filters

| Filter | Parameter Range | Implementation |
|---|---|---|
| Grayscale | On/Off | `SkColorFilters::Matrix` |
| Brightness | [-1.0, +1.0] | `SkColorFilters::Matrix` |
| Contrast | [-1.0, +1.0] | `SkColorFilters::Matrix` |
| Saturation | [-1.0, +1.0] | `SkColorFilters::Matrix` |

All four are combined into a single `SkColorFilter` using `SkColorFilters::Compose`.

### 7.2 Color Matrix Construction

```cpp
sk_sp<SkColorFilter> ImageFilterBuilder::build(const ImageElement& elem) {
    float sat = 1.0f + elem.filterSaturation;    // 0 = grayscale, 1 = normal
    if (elem.filterGrayscale) sat = 0.0f;

    float bright = elem.filterBrightness;         // additive offset
    float contr  = 1.0f + elem.filterContrast;    // scale around 0.5

    // Saturation matrix (derived from ITU-R BT.601 luminance weights)
    float Rw = 0.213f, Gw = 0.715f, Bw = 0.072f;
    float satMatrix[20] = {
        Rw + (1-Rw)*sat, Gw - Gw*sat,     Bw - Bw*sat,     0, 0,
        Rw - Rw*sat,     Gw + (1-Gw)*sat, Bw - Bw*sat,     0, 0,
        Rw - Rw*sat,     Gw - Gw*sat,     Bw + (1-Bw)*sat, 0, 0,
        0,               0,               0,               1, 0
    };

    // Contrast + brightness matrix: scale around mid-grey (0.5), then add brightness
    float contMatrix[20] = {
        contr, 0,     0,     0, (1.0f - contr) * 0.5f + bright,
        0,     contr, 0,     0, (1.0f - contr) * 0.5f + bright,
        0,     0,     contr, 0, (1.0f - contr) * 0.5f + bright,
        0,     0,     0,     1, 0
    };

    auto satFilter  = SkColorFilters::Matrix(satMatrix);
    auto contFilter = SkColorFilters::Matrix(contMatrix);
    return SkColorFilters::Compose(contFilter, satFilter);
}
```

### 7.3 Applying Filters at Render Time

Filters are applied at render time via `SkPaint::setColorFilter`. They do not modify the original GPU texture:

```cpp
void ImageElement::render(SkCanvas* canvas, float zoom) const {
    canvas->save();
    canvas->concat(buildRenderMatrix());

    SkPaint paint;
    paint.setAlphaf(opacity);
    paint.setColorFilter(ImageFilterBuilder::build(*this));

    SkSamplingOptions sampling(
        zoom < 1.0f ? SkFilterMode::kLinear : SkFilterMode::kLinear,
        SkMipmapMode::kLinear);

    canvas->drawImageRect(image.get(), srcRect, dstRect, sampling, &paint,
                           SkCanvas::kFast_SrcRectConstraint);
    canvas->restore();
}
```

### 7.4 Filter Presets

A set of named presets is provided in the image properties panel:

| Preset | Saturation | Brightness | Contrast |
|---|---|---|---|
| Original | 0 | 0 | 0 |
| Vivid | +0.4 | 0 | +0.2 |
| Muted | -0.3 | 0 | -0.1 |
| Black & White | Grayscale | 0 | 0 |
| Faded | -0.2 | +0.1 | -0.3 |
| High Contrast | 0 | 0 | +0.5 |

---

## 8. Image as Background vs Canvas Object

### 8.1 Two Roles

| Role | Z-Order | Behavior | Layer |
|---|---|---|---|
| `CanvasObject` | Among other elements in its layer | Selectable, movable, resizable | Any layer |
| `BackgroundImage` | Behind ALL layers | Not selectable in normal mode; fills page | Special `background` layer |

### 8.2 Setting as Background

```cpp
void ImageTool::setAsBackground(ElementId imgId) {
    ImageElement& elem = m_layerManager->getElement<ImageElement>(imgId);
    // Move to background layer
    m_layerManager->moveElement(imgId, m_activeLayerId, BACKGROUND_LAYER_ID);
    elem.role = ImageRole::BackgroundImage;
    // Resize to fill page
    elem.dstRect = m_layerManager->pageRect();
    elem.srcRect = SkRect::MakeWH(elem.naturalWidth, elem.naturalHeight);
    m_undoStack->push(std::make_unique<SetBackgroundCommand>(/*...*/));
}
```

### 8.3 Background Layer Behavior

The background layer (`BACKGROUND_LAYER_ID = 0`) is always rendered first, below all content layers. It is locked by default (not accidentally selectable). It can be unlocked from the Layers panel to replace the background image.

---

## 9. Pasting Images from Clipboard

### 9.1 Desktop (Qt)

```cpp
void ImageTool::pasteFromClipboard() {
    const QClipboard* cb = QApplication::clipboard();
    const QMimeData* mime = cb->mimeData();

    if (mime->hasImage()) {
        QImage qImg = qvariant_cast<QImage>(mime->imageData());
        if (qImg.isNull()) return;

        qImg = qImg.convertToFormat(QImage::Format_RGBA8888);
        DecodedImage decoded;
        decoded.width    = qImg.width();
        decoded.height   = qImg.height();
        decoded.channels = 4;
        decoded.pixels.assign(qImg.bits(), qImg.bits() + qImg.sizeInBytes());

        placeDecodedImage(decoded);
    }
    else if (mime->hasUrls()) {
        // Paste from file path on clipboard (e.g., Windows file copy)
        QString path = mime->urls().first().toLocalFile();
        importFromFile(path);
    }
}
```

### 9.2 Android — JNI Bridge

On Android, clipboard access for images requires a JNI call to the Java layer:

```java
// android/ClipboardBridge.java
public byte[] getClipboardImageBytes() {
    ClipboardManager cm = (ClipboardManager)
        context.getSystemService(Context.CLIPBOARD_SERVICE);
    ClipData clip = cm.getPrimaryClip();
    if (clip == null || clip.getItemCount() == 0) return null;

    ClipData.Item item = clip.getItemAt(0);
    Uri uri = item.getUri();
    if (uri == null) return null;

    try (InputStream is = context.getContentResolver().openInputStream(uri)) {
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        byte[] buf = new byte[4096];
        int read;
        while ((read = is.read(buf)) != -1) baos.write(buf, 0, read);
        return baos.toByteArray();
    } catch (Exception e) { return null; }
}
```

C++ side:

```cpp
void ImageTool::pasteFromAndroidClipboard() {
    JNIEnv* env = getJNIEnv();
    jclass cls   = env->FindClass("com/openboard/ClipboardBridge");
    jmethodID m  = env->GetMethodID(cls, "getClipboardImageBytes", "()[B");
    jbyteArray arr = (jbyteArray)env->CallObjectMethod(m_clipboardBridge, m);
    if (!arr) return;

    jsize len    = env->GetArrayLength(arr);
    jbyte* bytes = env->GetByteArrayElements(arr, nullptr);

    auto decoded = ImageDecoder::decode((uint8_t*)bytes, len);
    env->ReleaseByteArrayElements(arr, bytes, JNI_ABORT);

    placeDecodedImage(decoded);
}
```

---

## 10. Memory Management — GPU Texture Atlas and LRU Cache

### 10.1 TextureCache

`TextureCache` maintains a **Least Recently Used (LRU)** cache of GPU textures keyed by `ImageId`. When the cache exceeds its memory budget, the least recently accessed texture is evicted:

```cpp
class TextureCache {
public:
    TextureCache(size_t maxBytes);

    ImageId               insert(sk_sp<SkImage> image);
    sk_sp<SkImage>        get   (ImageId id);
    void                  evict (ImageId id);
    void                  clear ();

    size_t                usedBytes()  const { return m_usedBytes; }
    size_t                maxBytes()   const { return m_maxBytes; }

private:
    struct Entry {
        sk_sp<SkImage>    image;
        size_t            bytes;
        int64_t           lastAccess;   // monotonic timestamp
    };

    std::unordered_map<ImageId, Entry> m_cache;
    size_t                             m_usedBytes = 0;
    size_t                             m_maxBytes;

    void evictLRU();
};
```

Default maximum cache size: **256 MB** (adjustable via settings).

### 10.2 Eviction Policy

When inserting a new image would exceed `maxBytes`:

1. Find the entry with the oldest `lastAccess` timestamp.
2. Evict it (call `SkImage::~SkImage()` → GPU memory freed).
3. Repeat until enough space is available.

```cpp
void TextureCache::evictLRU() {
    while (!m_cache.empty() && m_usedBytes + pendingSize > m_maxBytes) {
        auto oldest = std::min_element(m_cache.begin(), m_cache.end(),
            [](const auto& a, const auto& b) {
                return a.second.lastAccess < b.second.lastAccess;
            });
        m_usedBytes -= oldest->second.bytes;
        m_cache.erase(oldest);
    }
}
```

### 10.3 Re-Upload After Eviction

If a texture has been evicted and is needed again (e.g., the user scrolled back to that area of the canvas), it is re-decoded from the original file and re-uploaded:

```cpp
sk_sp<SkImage> TextureCache::get(ImageId id) {
    auto it = m_cache.find(id);
    if (it != m_cache.end()) {
        it->second.lastAccess = monotonicMs();
        return it->second.image;
    }
    // Evicted: reload from source
    return reloadFromSource(id);
}
```

### 10.4 Texture Atlas (Small Images)

Images smaller than `ATLAS_THRESHOLD` (128 × 128 px) are packed into a **texture atlas** — a single large GPU texture containing many small images. This reduces GPU texture bind calls and improves rendering throughput for boards with many small images (e.g., inline icons):

```cpp
// TextureAtlas packs small images into a 2048x2048 texture
// using a shelf-packing algorithm (Next Fit Decreasing Height)
class TextureAtlas {
public:
    struct Region { SkIRect rect; };
    Region insert(const SkBitmap& bmp);
    sk_sp<SkImage> atlasImage() const { return m_atlasImage; }
private:
    SkBitmap       m_atlasBitmap;
    sk_sp<SkImage> m_atlasImage;
    int            m_currentShelfY = 0;
    int            m_currentShelfX = 0;
    int            m_currentShelfH = 0;
};
```

When drawing a small image, the `srcRect` into the atlas texture is used instead of a separate texture bind.

---

## 11. Lazy Loading for Large Files

### 11.1 Problem

Importing a 50 MP TIFF or multi-layer PSD (if supported) synchronously on the main thread would freeze the UI for several seconds during decode and GPU upload.

### 11.2 Two-Phase Loading

Large files (> `LARGE_FILE_THRESHOLD` = 4 MB compressed) use a two-phase approach:

**Phase 1 — Thumbnail (immediate, main thread)**:
A low-resolution thumbnail (max 256 × 256 px) is decoded synchronously from the file header (PNG/JPEG embed thumbnails) and displayed immediately:

```cpp
sk_sp<SkImage> ImageDecoder::decodeThumbnail(const uint8_t* data, size_t size,
                                              int maxDim)
{
    // stb_image supports resize via stbir
    int w, h, ch;
    stbi_uc* raw = stbi_load_from_memory(data, size, &w, &h, &ch, 4);
    float scale  = std::min(1.0f, (float)maxDim / std::max(w, h));
    int tw = w * scale, th = h * scale;
    std::vector<uint8_t> thumb(tw * th * 4);
    stbir_resize_uint8(raw, w, h, 0, thumb.data(), tw, th, 0, 4);
    stbi_image_free(raw);
    // Upload thumbnail to GPU
    return makeGPUImage(thumb.data(), tw, th);
}
```

**Phase 2 — Full resolution (async, background thread)**:
The full image is decoded on a background thread using a thread pool:

```cpp
void ImageTool::loadFullResolutionAsync(ImageId id, std::vector<uint8_t> fileData) {
    m_threadPool->enqueue([this, id, data = std::move(fileData)]() {
        auto decoded = ImageDecoder::decode(data.data(), data.size());

        // GPU upload must happen on main thread — post back
        m_mainThreadQueue->post([this, id, decoded = std::move(decoded)]() {
            auto gpuImg = ImageDecoder::uploadToGPU(decoded, m_grContext);
            m_textureCache->replace(id, gpuImg);
            m_renderEngine->invalidateAllContaining(id);  // trigger repaint
        });
    });
}
```

### 11.3 Progressive Reveal

During Phase 2 loading, the thumbnail is displayed with a subtle pulsing animation to indicate loading is in progress. When Phase 2 completes, the canvas region is invalidated and the full-resolution image replaces the thumbnail.

---

## 12. ImageTool API Reference

### 12.1 Full Class Declaration

```cpp
// tools/image/ImageTool.h
#pragma once
#include "BaseTool.h"
#include "ImageElement.h"
#include "ImageDecoder.h"
#include "TextureCache.h"
#include "CropTool.h"

class ImageTool : public BaseTool {
    Q_OBJECT
public:
    explicit ImageTool(LayerManager*, UndoStack*,
                       TextureCache*, GrDirectContext*,
                       QObject* parent = nullptr);
    ~ImageTool() override = default;

    // BaseTool interface
    void onActivate()                       override;
    void onDeactivate()                     override;
    void onPointerDown(const PointerEvent&) override;
    void onPointerMove(const PointerEvent&) override;
    void onPointerUp  (const PointerEvent&) override;
    void onPointerCancel()                  override;
    void renderOverlay(SkCanvas*)           override;

    // Import operations
    void importFromFile       (const QString& filePath);
    void importFromData       (const uint8_t* data, size_t size,
                               const QString& hint = "");
    void pasteFromClipboard   ();
    void pasteFromAndroidClipboard();

    // Editing
    void setAsBackground      (ElementId id);
    void enterCropMode        (ElementId id);
    void exitCropMode         (bool commit);
    void flipHorizontal       (ElementId id);
    void flipVertical         (ElementId id);
    void setOpacity           (ElementId id, float opacity);

    // Filter control (0.0 = no change for brightness/contrast/saturation)
    void setFilterGrayscale   (ElementId id, bool enabled);
    void setFilterBrightness  (ElementId id, float value);
    void setFilterContrast    (ElementId id, float value);
    void setFilterSaturation  (ElementId id, float value);
    void resetFilters         (ElementId id);

signals:
    void imageImported        (ElementId id);
    void imageLoadProgress    (ElementId id, float progress);
    void imageLoadComplete    (ElementId id);

private:
    void placeDecodedImage    (const DecodedImage& decoded, SkPoint center = {});
    sk_sp<SkImage> uploadAsync(const DecodedImage& decoded, ImageId id);
    void onHandleDrag         (HandleType h, SkPoint pos, bool shiftKey);
    void updateHandlePositions();
    void renderHandles        (SkCanvas*);

    LayerManager*    m_layerManager;
    UndoStack*       m_undoStack;
    TextureCache*    m_textureCache;
    GrDirectContext* m_grContext;
    CropTool*        m_cropTool;

    ImageElement*    m_editElem      = nullptr;
    ElementId        m_editId        = INVALID_ID;
    bool             m_isCropping    = false;
    HandleType       m_activeHandle  = HandleType::None;
    bool             m_isDragging    = false;
    SkPoint          m_dragStart;
    SkRect           m_dragOrigRect;
    float            m_viewportZoom  = 1.0f;

    std::array<TransformHandle, 9> m_handles;
    ThreadPool       m_threadPool;
    MainThreadQueue* m_mainQueue;
};
```

### 12.2 Rendering Path Summary

```
ImageElement::render(canvas, zoom)
    |
    +-- canvas->save()
    +-- canvas->concat(buildRenderMatrix())  // flip + rotation
    |
    +-- Build SkColorFilter (brightness/contrast/saturation/grayscale)
    +-- Set paint.setAlphaf(opacity)
    +-- Set paint.setColorFilter(filter)
    +-- Set SkSamplingOptions (bilinear + mipmap)
    |
    +-- canvas->drawImageRect(image, srcRect, dstRect, sampling, &paint)
    |
    +-- canvas->restore()
```

---

*End of 06F — Image Tool*
