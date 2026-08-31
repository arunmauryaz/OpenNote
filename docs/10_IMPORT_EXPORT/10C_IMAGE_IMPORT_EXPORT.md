# 10C — Image Import & Export

## Overview

Provides high-speed raster image decoding and encoding capabilities for document exchange.

---

## Supported Formats

- **Import**: PNG, JPEG, WebP, BMP, GIF (first frame), TIFF.
- **Export**: PNG (lossless with transparency), JPEG (compressed 1-100%), WebP (lossless or lossy).

---

## Technical Specifications

1. **Decoding**: Handled via `stb_image` for lightweight formats, supplemented by native Android `ImageDecoder` via JNI for WebP/HEIC.
2. **Export Resolution Scaling**: Export targets can be generated at custom multipliers (1x, 2x, 4x, or targeted DPI like 300/600 DPI) by allocating an offscreen `SkSurface` of the corresponding pixel dimensions.
3. **Region Export**: Supports exporting a custom cropped selection marquee bounding box rather than the full canvas.
