# 10A — PDF Import Engine

## Overview

The PDF Import subsystem converts multi-page PDF documents into OpenBoard pages or infinite canvas elements. PDF rendering is executed by **PDFium** (Google's open-source C++ PDF library).

---

## Technical Flow

```
PDF File Path -> PDFium (FPDF_LoadDocument) -> Loop Pages -> FPDF_RenderPageBitmap -> SkImage GPU Texture -> OpenBoard Layer Element
```

---

## Key Implementation Details

1. **Lazy Page Rendering**: Pages are not rendered all at once to prevent Memory Out-of-Bounds errors. Pages are converted to Skia textures on-demand as the user scrolls to that page index.
2. **DPI Selection**:
   - Standard: 150 DPI (Fast, balanced memory).
   - High-Res / Print: 300 DPI (Sharp text, higher RAM usage).
3. **Password-Protected PDFs**: If `FPDF_LoadDocument` returns `FPDF_ERR_PASSWORD`, the engine prompts the UI for password input via JNI callback.
4. **Vector vs Raster**: Background pages store PDFium references so vector zoom re-rendering can trigger dynamically at zoom levels > 300%.
