# 🔌 OpenBoard Universal Plugin SDK — Complete Developer Guide

> **This is the Master Specification for OpenBoard Plugins.**
> Any developer or AI coding agent can read this single document and build fully compliant plugins for OpenBoard without requiring any other context.

---

## 📌 What Plugins Can Do in OpenBoard

OpenBoard plugins have **unrestricted UI & Engine extension access**. A plugin can:

1. 🧰 **Extend the Main Toolbar Dock**: Inject custom tool buttons (3D shapes, laser tools, math graphers).
2. ✒️ **Extend the Pen Flyout**: Add custom pen types (AI Handwriting Auto-Corrector, Calligraphy Pen, Fluent Pen, Neon Marker).
3. 💬 **Extend the 3-Dot Main Menu**: Add options under File/Export/Share (e.g. "Share via QR Code", "Sync to VPS Server").
4. 📂 **Add Custom File Format Support**: Register custom file importers & exporters (.docx, .psd, custom binary).
5. 🤖 **Intercept & Transform Ink (AI Hooks)**: Intercept handwriting strokes in real-time, transform ink into typed text elements, auto-beautify shapes, or apply neural smoothers.
6. 🎨 **Draw Direct Canvas Overlays**: Hook into the Skia render loop to draw floating 3D objects, HUD widgets, or custom grids.
7. 🪟 **Inject Custom UI Panels**: Add sliding side panels, floating dialogs, or custom tool properties.

---

## 📁 Plugin Package Directory Structure

A complete OpenBoard plugin package (extension `.obplugin` or directory inside `[app]/plugins/`) has the following layout:

```
com.example.myadvancedplugin/
├── plugin.json               ← Plugin Manifest (metadata, UI injection, settings)
├── libmyadvancedplugin.so    ← Compiled Shared Library for Android (arm64-v8a)
├── myadvancedplugin.dll      ← Compiled Shared Library for Windows (x64)
├── icon_tool.png             ← Toolbar Icon (48x48 PNG)
├── icon_pen.png              ← Pen Sub-menu Icon (48x48 PNG)
└── icon_share.png            ← 3-Dot Menu Icon (48x48 PNG)
```

---

## 📄 Complete C API Interface (`openboard_plugin/plugin_api.h`)

This pure-C ABI header is supplied by the SDK. Copy or include this header in your plugin source code:

```c
#ifndef OPENBOARD_PLUGIN_API_H
#define OPENBOARD_PLUGIN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OB_PLUGIN_API_VERSION 1

// --- Basic Geometry & Data Types ---
typedef struct OBPoint {
    float x;
    float y;
    float pressure;
    float tiltX;
    float tiltY;
    uint64_t timestamp;
} OBPoint;

typedef struct OBRect { float x; float y; float w; float h; } OBRect;
typedef struct OBColor { uint8_t r; uint8_t g; uint8_t b; uint8_t a; } OBColor;

// --- Plugin Metadata Struct ---
typedef struct OBPluginInfo {
    const char* id;           // Unique ID: "com.author.plugin"
    const char* name;         // Display Name
    const char* version;      // Version string "1.0.0"
    int         apiVersion;   // Must be OB_PLUGIN_API_VERSION (1)
    uint32_t    pluginTypes;  // Bitmask of supported types
} OBPluginInfo;

// --- UI Contribution Injection Points ---
typedef enum OBUIContributionPoint {
    UI_POINT_TOOLBAR_DOCK   = 1, // Main bottom/side tool dock
    UI_POINT_PEN_FLYOUT     = 2, // Pen options flyout panel
    UI_POINT_OVERFLOW_MENU  = 3, // 3-Dot main menu (File / Share options)
    UI_POINT_SIDE_PANEL     = 4, // Sliding side drawer
    UI_POINT_SELECTION_BAR  = 5, // Floating selection action bar
    UI_POINT_LAYER_MENU     = 6  // Layer panel item context menu
} OBUIContributionPoint;

// --- Hook Action Return Codes ---
typedef enum OBHookResult {
    OB_HOOK_CONTINUE = 0,        // Proceed with normal engine processing
    OB_HOOK_CANCEL   = 1,        // Cancel engine processing for this event
    OB_HOOK_REPLACE  = 2         // Stroke was replaced/transformed by plugin
} OBHookResult;

// --- Plugin Context (Host Engine Callbacks) ---
typedef struct OBPluginContext {
    // --- Canvas Manipulation ---
    void (*getViewport)(void* ctx, OBRect* outRect);
    void (*getActiveLayerId)(void* ctx, int* outId);
    
    // Add raw stroke to active layer
    void (*addStroke)(void* ctx, const OBPoint* points, int count, OBColor color, float width);
    
    // Add typed text element to active layer
    void (*addTextElement)(void* ctx, const char* text, float x, float y, float fontSize, OBColor color);
    
    // Add raster image element to canvas
    void (*addImageElement)(void* ctx, const uint8_t* rgbaPixels, int w, int h, float x, float y, float wWorld, float hWorld);
    
    // Delete stroke by pointer handle
    void (*deleteElement)(void* ctx, uint32_t elementId);

    // --- Undo/Redo Batching ---
    void (*beginUndoGroup)(void* ctx, const char* description);
    void (*endUndoGroup)(void* ctx);

    // --- Settings Storage ---
    const char* (*getSetting)(void* ctx, const char* key);
    void        (*setSetting)(void* ctx, const char* key, const char* value);
    const char* (*getPluginDataDir)(void* ctx);

    // --- User Interface Notifications & Dialogs ---
    void (*showToast)(void* ctx, const char* message);
    void (*showProgress)(void* ctx, float fraction, const char* label);
    void (*hideProgress)(void* ctx);
    void (*openCustomPanel)(void* ctx, const char* panelId);

    // --- Export Helpers ---
    int  (*exportPageToRGBA)(void* ctx, int pageIndex, int dpi, uint8_t** outPixels, int* outW, int* outH);
    void (*freeExportedData)(void* ctx, uint8_t* pixels);

    void* internal; // Reserved for engine handle
} OBPluginContext;

// ============================================================================
// Mandated Plugin Exports (Functions your library MUST export)
// ============================================================================

// 1. Return Plugin Metadata
OBPluginInfo* ob_plugin_get_info(void);

// 2. Initialize Plugin (called when loaded)
int ob_plugin_initialize(OBPluginContext* ctx);

// 3. Shutdown Plugin (called before unloading)
void ob_plugin_shutdown(OBPluginContext* ctx);

// 4. (Optional) Custom UI Action Trigger
// Called when user clicks an injected UI button (Toolbar, Pen flyout, or 3-Dot menu item)
void ob_plugin_on_ui_action(OBPluginContext* ctx, const char* actionId);

// 5. (Optional) Real-time Stroke Interceptor / AI Handwriting Transformer Hook
// Called on TOUCH_UP before stroke is committed to layer
OBHookResult ob_plugin_on_pre_stroke_commit(OBPluginContext* ctx, const OBPoint* points, int count, OBColor color, float width);

// 6. (Optional) Direct Render Overlay Hook (Skia canvas draw)
// Render custom visual overlays over the canvas
void ob_plugin_on_render_overlay(OBPluginContext* ctx, void* skCanvasHandle, const OBRect* viewport);

#ifdef __cplusplus
}
#endif

#endif // OPENBOARD_PLUGIN_API_H
```

---

## 🛠️ Complete Code Example 1: Custom Pen Flyout Tool — AI Handwriting Corrector

This plugin injects an **"AI Handwriting Corrector"** option directly into the Pen Flyout menu. When active, raw handwriting strokes are intercepted, converted into crisp typed text, and placed on the canvas!

### `plugin.json`
```json
{
  "id": "com.ai.handwriting.fixer",
  "name": "AI Ink-to-Text Fixer",
  "version": "1.0.0",
  "apiVersion": "1",
  "author": "AI Systems Inc.",
  "description": "Adds an AI Pen to the Pen Flyout menu that automatically corrects rough handwriting into clean typed text.",
  "entryPoint": "libai_handwriting.so",
  "permissions": ["CANVAS_READ", "CANVAS_WRITE"],
  "uiContributions": [
    {
      "point": "UI_POINT_PEN_FLYOUT",
      "id": "ai_pen_fixer",
      "label": "AI Handwriting Corrector Pen",
      "icon": "icon_pen.png"
    }
  ]
}
```

### `ai_handwriting.c`
```c
#include "openboard_plugin/plugin_api.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static OBPluginInfo g_info = {
    .id         = "com.ai.handwriting.fixer",
    .name       = "AI Ink-to-Text Fixer",
    .version    = "1.0.0",
    .apiVersion = OB_PLUGIN_API_VERSION,
    .pluginTypes= 1
};

static bool g_ai_pen_active = false;

OBPluginInfo* ob_plugin_get_info(void) {
    return &g_info;
}

int ob_plugin_initialize(OBPluginContext* ctx) {
    ctx->showToast(ctx, "AI Handwriting Corrector Plugin Loaded!");
    return 0;
}

void ob_plugin_shutdown(OBPluginContext* ctx) {
    g_ai_pen_active = false;
}

// User selected the AI Pen inside the Pen Flyout menu!
void ob_plugin_on_ui_action(OBPluginContext* ctx, const char* actionId) {
    if (strcmp(actionId, "ai_pen_fixer") == 0) {
        g_ai_pen_active = !g_ai_pen_active;
        if (g_ai_pen_active) {
            ctx->showToast(ctx, "AI Handwriting Corrector Pen: ACTIVE");
        } else {
            ctx->showToast(ctx, "AI Pen Deactivated");
        }
    }
}

// Real-Time Stroke Interceptor: Replaces raw stroke with typed text element!
OBHookResult ob_plugin_on_pre_stroke_commit(OBPluginContext* ctx, const OBPoint* points, int count, OBColor color, float width) {
    if (!g_ai_pen_active || count < 5) {
        return OB_HOOK_CONTINUE; // Standard stroke handling
    }

    // 1. Calculate bounding box of user's ink
    float minX = points[0].x, maxX = points[0].x;
    float minY = points[0].y, maxY = points[0].y;
    for (int i = 1; i < count; i++) {
        if (points[i].x < minX) minX = points[i].x;
        if (points[i].x > maxX) maxX = points[i].x;
        if (points[i].y < minY) minY = points[i].y;
        if (points[i].y > maxY) maxY = points[i].y;
    }

    // 2. Perform OCR / Handwriting Recognition (Mock algorithm)
    const char* recognizedText = "Sample Text";

    // 3. Begin Undo Group & Insert Typed Text Element
    ctx->beginUndoGroup(ctx, "AI Handwriting Correction");
    
    // Add crisp text at ink location
    ctx->addTextElement(ctx, recognizedText, minX, minY, 36.0f, color);
    
    ctx->endUndoGroup(ctx);

    ctx->showToast(ctx, "Handwriting converted to Text!");

    // 4. Return OB_HOOK_REPLACE so the original raw ink stroke is discarded!
    return OB_HOOK_REPLACE;
}
```

---

## 📲 Complete Code Example 2: 3-Dot Overflow Menu Injection — Share via VPS & QR Code

Injects a **"Share via QR Code (VPS)"** item into the top-right 3-dot menu. Exports the board, uploads to a VPS server via FTP, and displays a QR code for mobile users to scan and download!

### `plugin.json`
```json
{
  "id": "com.developer.qrshare",
  "name": "VPS QR Share Plugin",
  "version": "1.0.0",
  "apiVersion": "1",
  "author": "Cloud Systems",
  "description": "Uploads board image to VPS FTP and generates a scannable QR Code.",
  "entryPoint": "libqrshare.so",
  "permissions": ["CANVAS_READ", "NETWORK"],
  "uiContributions": [
    {
      "point": "UI_POINT_OVERFLOW_MENU",
      "id": "action_qr_share",
      "label": "Share Board via QR Code (VPS)",
      "icon": "icon_qr.png"
    }
  ],
  "settingsSchema": {
    "fields": [
      {
        "key": "vps_url",
        "type": "url",
        "label": "VPS FTP/HTTP Endpoint",
        "placeholder": "https://my-vps-server.com/upload",
        "required": true
      },
      {
        "key": "vps_key",
        "type": "password",
        "label": "API Key / Password"
      }
    ]
  }
}
```

### `qrshare.c`
```c
#include "openboard_plugin/plugin_api.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static OBPluginInfo g_info = {
    .id         = "com.developer.qrshare",
    .name       = "VPS QR Share Plugin",
    .version    = "1.0.0",
    .apiVersion = OB_PLUGIN_API_VERSION,
    .pluginTypes= 2
};

OBPluginInfo* ob_plugin_get_info(void) { return &g_info; }
int ob_plugin_initialize(OBPluginContext* ctx) { return 0; }
void ob_plugin_shutdown(OBPluginContext* ctx) {}

// Triggered when user selects "Share Board via QR Code" from 3-dot overflow menu
void ob_plugin_on_ui_action(OBPluginContext* ctx, const char* actionId) {
    if (strcmp(actionId, "action_qr_share") != 0) return;

    // Step 1: Export active canvas to high-res RGBA pixel buffer (150 DPI)
    ctx->showProgress(ctx, 0.2f, "Rendering Canvas Image...");
    uint8_t* pixels = NULL;
    int width = 0, height = 0;
    
    if (ctx->exportPageToRGBA(ctx, 0, 150, &pixels, &width, &height) != 0) {
        ctx->hideProgress(ctx);
        ctx->showToast(ctx, "Error: Failed to export canvas.");
        return;
    }

    // Step 2: Upload pixels to VPS server (Simulated network upload)
    ctx->showProgress(ctx, 0.6f, "Uploading to VPS Server...");
    const char* vpsUrl = ctx->getSetting(ctx, "vps_url");
    
    // (In production: Use libcurl / sockets to POST pixels to vpsUrl)

    // Step 3: Free memory & Display QR Panel
    ctx->freeExportedData(ctx, pixels);
    ctx->showProgress(ctx, 1.0f, "Done!");
    ctx->hideProgress(ctx);

    // Tell UI to open the floating QR display dialog
    ctx->openCustomPanel(ctx, "qr_code_display_dialog");
    ctx->showToast(ctx, "Scan QR Code to download!");
}
```

---

## 🛠️ Complete Code Example 3: Main Toolbar Dock Injection — 3D Shape Tool

Injects a **"3D Shape Builder"** button directly into the main bottom toolbar dock!

### `plugin.json` snippet
```json
"uiContributions": [
  {
    "point": "UI_POINT_TOOLBAR_DOCK",
    "id": "tool_3d_cube",
    "label": "3D Cube Creator",
    "icon": "icon_3d.png"
  }
]
```

### `3d_shape.c` snippet
```c
void ob_plugin_on_ui_action(OBPluginContext* ctx, const char* actionId) {
    if (strcmp(actionId, "tool_3d_cube") != 0) return;

    // Get current screen center in world coordinates
    OBRect vp;
    ctx->getViewport(ctx, &vp);
    float centerX = vp.x + vp.w / 2.0f;
    float centerY = vp.y + vp.h / 2.0f;

    ctx->beginUndoGroup(ctx, "Insert 3D Cube");

    // Add front face of 3D cube
    float size = 150.0f;
    float depth = 50.0f;
    OBColor blue = {0, 120, 255, 255};

    OBPoint frontRect[5] = {
        {centerX, centerY, 1.0f},
        {centerX + size, centerY, 1.0f},
        {centerX + size, centerY + size, 1.0f},
        {centerX, centerY + size, 1.0f},
        {centerX, centerY, 1.0f}
    };
    ctx->addStroke(ctx, frontRect, 5, blue, 4.0f);

    // Add connecting depth lines
    OBPoint depthLine1[2] = {{centerX, centerY, 1.0f}, {centerX + depth, centerY - depth, 1.0f}};
    ctx->addStroke(ctx, depthLine1, 2, blue, 3.0f);

    ctx->endUndoGroup(ctx);
    ctx->showToast(ctx, "3D Cube inserted onto canvas!");
}
```

---

## 🔨 Compiling Plugins with CMake

Use this `CMakeLists.txt` template to build your plugin for Android & Windows:

```cmake
cmake_minimum_required(VERSION 3.22)
project(openboard_plugin LANGUAGES C CXX)

# Include SDK C API Header
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Compile as shared library (.so on Android, .dll on Windows)
add_library(myplugin SHARED
    src/plugin_main.c
)

set_target_properties(myplugin PROPERTIES
    PREFIX "lib"
    C_VISIBILITY_PRESET default
)

if(ANDROID)
    target_link_libraries(myplugin log)
endif()
```

### Build Commands

**For Android (arm64-v8a):**
```bash
cmake -B build/android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
cmake --build build/android
```

**For Windows (x64):**
```bash
cmake -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Release
```

---

## ⚡ Summary Checklist for AI Coding Platforms & Developers

When generating a plugin for OpenBoard:
- [x] Create a `plugin.json` with unique `id`, UI injection declarations (`uiContributions`), and optional `settingsSchema`.
- [x] Export `ob_plugin_get_info()`, `ob_plugin_initialize()`, and `ob_plugin_shutdown()`.
- [x] Use `ob_plugin_on_ui_action()` to handle clicks from injected buttons (Toolbar, Pen flyout, or 3-Dot menu).
- [x] Use `ob_plugin_on_pre_stroke_commit()` to intercept ink strokes for AI handwriting fixers or shape recognizers.
- [x] Always wrap canvas modifications between `ctx->beginUndoGroup()` and `ctx->endUndoGroup()`.
- [x] Package into a single directory or `.obplugin` zip file.
