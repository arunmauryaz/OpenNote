# 14 — Windows Platform Layer

## Overview

The Windows layer provides native desktop capabilities using C++ / WinUI 3 or classic Win32 HWND integration. It communicates directly with the `libOpenBoard` static/shared library without JNI marshalling overhead.

---

## Architectural Details

1. **Rendering**: Interfacing Skia with DirectX 11 / 12 via Vulkan or ANGLE / WGL OpenGL context on a dedicated Win32 render thread.
2. **Pen & Touch API**: Consumes `WM_POINTERDOWN`, `WM_POINTERUPDATE`, `WM_POINTERUP` messages to capture Windows Ink pressure and tilt values.
3. **High-DPI Awareness**: Per-Monitor v2 DPI awareness (`SetThreadDpiAwarenessContext`) dynamically updates camera scale on display transfers.
4. **Keyboard Shortcuts**: Native accelerator tables for `Ctrl+Z` (Undo), `Ctrl+Y` (Redo), `Ctrl+S` (Save), `Ctrl+O` (Open), `Space+Drag` (Pan).
