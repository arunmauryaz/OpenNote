# 11 — Advanced Plugin Architecture & Universal UI Injection System

## Overview

The OpenBoard Plugin Architecture provides deep, non-invasive extensibility across every layer of the application:
- **Engine Layer**: Custom stroke transformers, AI handwriting beautification, specialized ink shaders, file import/export hooks.
- **UI Contribution System**: Universal UI insertion points that allow plugins to inject buttons, flyout tools, menu items, side panels, and context menus anywhere in the app.
- **Render Loop Overlays**: Direct Skia canvas draw hooks for custom visual overlays (3D viewports, widgets, HUD elements).

Plugins are compiled shared libraries (`.so` on Android, `.dll` on Windows) communicating via a stable, versioned C ABI (`openboard_plugin/plugin_api.h`).

---

## 🏛️ Comprehensive Extension Architecture

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                             PLATFORM UI LAYER                                    │
│   (Android Jetpack Compose / Windows WinUI 3)                                    │
│                                                                                  │
│   UI Contribution Registry (Dynamic Menu & Toolbar Injector)                     │
│   ┌──────────────────────┬──────────────────────┬─────────────────────────────┐  │
│   │ Main Toolbar Dock    │ Pen Sub-menu Flyout  │ 3-Dot Overflow Menu (Top)   │  │
│   │ [Plugin Custom Tools]│ [Plugin Custom Pens] │ [Plugin Custom Importers/   │  │
│   │                      │ [AI Handwriting Pen] │  Exporters & Share Options] │  │
│   └──────────────────────┴──────────────────────┴─────────────────────────────┘  │
└──────────────────────────────────────┬───────────────────────────────────────────┘
                                       │ Direct Event & Registration Sync
                                       ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│                   libOpenBoard (C++ Core Engine)                                  │
│                                                                                  │
│   ┌──────────────────────────────────────────────────────────────────────────┐   │
│   │                           PluginManager                                  │   │
│   │  - Dynamic Loader (dlopen / LoadLibrary)                                 │   │
│   │  - UI & Hook Registration Registry                                       │   │
│   │  - Sandboxed Context Provider                                            │   │
│   └──────────────────────────────────┬───────────────────────────────────────┘   │
│                                      │                                           │
│   ┌──────────────────────────────────┴───────────────────────────────────────┐   │
│   │                           Pipeline Hooks                                 │   │
│   │  ┌──────────────────────┬──────────────────────┬──────────────────────┐  │   │
│   │  │ Stroke Interceptor   │ Render Overlay       │ File I/O Pipeline    │  │   │
│   │  │ (AI Ink -> Text,     │ (3D Shapes, HUD,     │ (Custom Formats,     │  │   │
│   │  │  Shape Recognizer)   │  Custom Skia Draw)   │  Cloud Sync)         │  │   │
│   │  └──────────────────────┴──────────────────────┴──────────────────────┘  │   │
│   └──────────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## 🎯 UI Contribution Points (UI Placement Registry)

Plugins declare UI contributions in `plugin.json` or register them dynamically at runtime via C API calls. The platform UI layer automatically builds and renders the corresponding controls.

| Contribution ID | Visual Location | Example Usage |
|-----------------|-----------------|---------------|
| `UI_POINT_TOOLBAR_DOCK` | Main bottom tool dock | Add new tool buttons (e.g., 3D Shapes, Math Graph, Laser Marker) |
| `UI_POINT_PEN_FLYOUT` | Pen sub-menu options panel | Add custom pen styles (e.g., Fluent Pen, Calligraphy, AI Handwriting Fixer) |
| `UI_POINT_OVERFLOW_MENU` | Top 3-Dot main menu | Add options under File/Share (e.g., "Share via QR Code", "Sync to VPS") |
| `UI_POINT_SIDE_PANEL` | Sliding side panel / drawer | Add rich side views (e.g., AI Assistant, Symbol Library, Cloud File Browser) |
| `UI_POINT_SELECTION_BAR` | Floating selection context bar | Add actions for selected items (e.g., "Convert Ink to Text", "Auto-Vectorize") |
| `UI_POINT_LAYER_MENU` | Layer panel item menu | Add layer operations (e.g., "Apply Neural Filter", "Export Layer Only") |
| `UI_POINT_CANVAS_OVERLAY` | Direct canvas render space | Draw floating UI widgets, 3D manipulation handles, or HUD grids |

---

## 🪝 Engine Pipeline Hooks

Plugins can subscribe to engine lifecycle events to intercept, transform, or enhance behavior.

```c
typedef enum OBHookType {
    // Intercept stroke before it is committed to the layer
    // (Allows transforming raw ink into polished geometry or typed text)
    OB_HOOK_PRE_STROKE_COMMIT  = 1,

    // Called after a stroke is committed
    OB_HOOK_POST_STROKE_COMMIT = 2,

    // Intercept canvas render pass (Draw custom graphics onto SkCanvas)
    OB_HOOK_CANVAS_RENDER_OVERLAY = 3,

    // Intercept selection modification
    OB_HOOK_SELECTION_CHANGED  = 4,

    // Custom File Importer/Exporter invocation
    OB_HOOK_FILE_IMPORT        = 5,
    OB_HOOK_FILE_EXPORT        = 6
} OBHookType;
```

---

## 🧠 AI Ink Transformer Example (Raw Handwriting \(\rightarrow\) Corrected Text)

When a plugin registers `OB_HOOK_PRE_STROKE_COMMIT`, it receives the point buffer of a completed stroke before it reaches the layer.

```
User draws rough letter 'A'
        │
        ▼
Engine triggers: OB_HOOK_PRE_STROKE_COMMIT
        │
        ▼
Plugin AI Engine processes stroke point cloud:
  1. Recognized character = 'A' (Confidence: 98%)
  2. Plugin cancels stroke commit (returns OB_HOOK_REPLACE)
  3. Plugin calls ctx->addTextElement(ctx, "A", x, y, fontSize, fontColor)
        │
        ▼
Canvas updates instantly with crisp, formatted text instead of rough ink!
```

---

## 📋 Comprehensive `plugin.json` Manifest Schema

```json
{
  "id": "com.developer.superplugin",
  "name": "Super Board Plugin",
  "version": "1.0.0",
  "apiVersion": "1",
  "author": "AI Developer Platform",
  "description": "Extends toolbar, adds AI handwriting correction pen, QR share in 3-dot menu, and 3D shapes.",
  "types": ["TOOL_PLUGIN", "SHARING_PLUGIN", "EXPORTER_PLUGIN", "FILTER_PLUGIN"],
  "entryPoint": "libsuperplugin.so",
  "permissions": ["CANVAS_READ", "CANVAS_WRITE", "NETWORK", "CLIPBOARD"],
  "uiContributions": [
    {
      "point": "UI_POINT_PEN_FLYOUT",
      "id": "ai_handwriting_pen",
      "label": "AI Handwriting Corrector",
      "icon": "icon_ai_pen.png"
    },
    {
      "point": "UI_POINT_TOOLBAR_DOCK",
      "id": "tool_3d_shapes",
      "label": "3D Shape Builder",
      "icon": "icon_3d.png"
    },
    {
      "point": "UI_POINT_OVERFLOW_MENU",
      "id": "share_qr_vps",
      "label": "Share via QR Code (VPS)",
      "category": "SHARE",
      "icon": "icon_qr.png"
    }
  ],
  "settingsSchema": {
    "fields": [
      {
        "key": "vps_ftp_host",
        "type": "string",
        "label": "VPS FTP/HTTP Server URL",
        "placeholder": "https://share.myvps.com",
        "required": true
      },
      {
        "key": "ai_engine_mode",
        "type": "choice",
        "label": "AI Handwriting Mode",
        "options": [
          {"value": "text_conversion", "label": "Convert Ink to Typed Text"},
          {"value": "shape_beautifier", "label": "Auto-Perfect Lines & Circles"},
          {"value": "smoothing_only", "label": "Ultra-Smooth Curves"}
        ],
        "default": "text_conversion"
      }
    ]
  }
}
```

---

## 🛡️ Sandbox & Threading Rules

1. **Thread Safety**: UI contributions execute on the main UI thread; stroke hooks execute on the Engine Render Thread.
2. **Crash Resilience**: If a plugin hook throws an unhandled native exception or SEGFAULT, `PluginManager` isolates the fault, unloads the plugin, and notifies the UI via toast without crashing the main whiteboard process.
