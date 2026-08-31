# 00 — OpenBoard: Project Overview

> **Document version:** 1.0.0
> **Last updated:** 2026-08-04
> **Status:** Living document — update with every major architectural decision

---

## Table of Contents

1. [What Is OpenBoard?](#1-what-is-openboard)
2. [Goals vs. Competitors](#2-goals-vs-competitors)
3. [Core Design Philosophy](#3-core-design-philosophy)
4. [Why C++?](#4-why-c)
5. [Platform Strategy](#5-platform-strategy)
6. [Key Differentiators](#6-key-differentiators)
7. [Three-Layer Architecture](#7-three-layer-architecture)
8. [Module Dependency Diagram](#8-module-dependency-diagram)
9. [Non-Goals](#9-non-goals)
10. [Open Source Strategy](#10-open-source-strategy)

---

## 1. What Is OpenBoard?

**OpenBoard** is a high-performance, cross-platform digital whiteboard engine written entirely in C++. It is designed as a **library-first** product: a single compiled engine that powers native applications on Android, Windows, macOS, Linux, and (eventually) iOS, all sharing the same rendering, input-handling, storage, undo/redo, and plugin logic.

OpenBoard is **not** a whiteboard application. It is the engine that makes whiteboard applications fast, consistent, and extensible. A thin platform shell (Kotlin on Android, Win32/WinUI on Windows) provides only the OS-level plumbing: a surface to draw on, a way to receive touch/pointer events, and file-system access. Everything else — stroke rendering, layer management, tool logic, collaborative operations, PDF/image import, plugin lifecycle — runs inside the engine.

Think of it like **Chromium vs. Chrome**: OpenBoard is Chromium, and any product built on top of it is Chrome.

### Project Motivation

Modern whiteboard apps suffer from three recurring failure modes:

| Failure Mode | Example |
|---|---|
| Poor performance on older/budget Android devices | Stroke lag exceeding 16 ms causing visible smearing |
| Inconsistent behavior across platforms | Tools that work on web but break in the desktop app |
| Closed ecosystems with no plugin surface | Cannot add custom shape libraries or export formats |

OpenBoard is built to eliminate all three.

---

## 2. Goals vs. Competitors

### Competitive Landscape

| Product | Architecture | Engine | Plugin System | Offline | Open Source |
|---|---|---|---|---|---|
| **MyViewBoard** | Electron / Web | Browser canvas | None | Partial | No |
| **Whiteboard Fox** | Web-only (Canvas2D) | Browser canvas | None | No | No |
| **Miro** | Web SPA + native wrapper | Browser WebGL | Limited REST API | No | No |
| **Jamboard** (EOL) | Web + Android native | Skia (via Flutter) | None | Limited | No |
| **Excalidraw** | React + Canvas | Browser canvas | Experimental | Yes (PWA) | Yes |
| **Noteshelf / GoodNotes** | Swift/iOS-native | Custom (iOS only) | None | Yes | No |
| **OpenBoard (this project)** | C++ engine + thin shell | Skia (native) | Full binary ABI | Yes | Yes |

### What We Are Building Toward

- **Stroke latency <= 4 ms** from input event to pixel commit on a mid-range Android device (Snapdragon 695, 90 Hz display).
- **Zero-copy path** from stylus input to GPU vertex buffer for active stroke rendering.
- **Deterministic replay** — any sequence of input events produces byte-identical output on every platform.
- **Plugin-safe memory** — plugins cannot corrupt engine state because they communicate through a versioned ABI, not raw pointers.

---

## 3. Core Design Philosophy

### The Thin Shell Principle

> **The C++ engine does EVERYTHING. The platform UI is a thin shell.**

This is the single most important architectural constraint of the project. It must be actively defended against feature creep in platform layers.

**What "thin shell" means in practice:**

```
+---------------------------------------------------------------------+
|  Platform Shell (Kotlin / Win32 / Swift)                            |
|  -----------------------------------------------------------------  |
|  Allowed:  Surface creation, event forwarding, OS permissions,      |
|            file picker dialogs, share sheets, in-app purchase.      |
|                                                                     |
|  NOT Allowed: Stroke logic, layer ordering, undo stack, tool        |
|               state, rendering commands, plugin calls.              |
+---------------------------------------------------------------------+
                          |  JNI / FFI (C ABI)
                          v
+---------------------------------------------------------------------+
|  C++ Engine (libOpenBoard.so / OpenBoard.dll)                       |
|  -----------------------------------------------------------------  |
|  Canvas, Tools, Layers, History, Rendering, Input, FileIO, Plugins  |
+---------------------------------------------------------------------+
```

**Why this matters:**

1. **Feature parity is automatic.** A bug fix or new tool added to the engine is immediately available on all platforms without separate implementations.
2. **Testing is centralized.** 95%+ of unit and integration tests target the C++ layer. Platform tests are limited to bridge correctness.
3. **Performance is predictable.** No JVM GC, no JavaScript event loop, no Electron overhead between input and render.

### Immutable Event Sourcing for Document State

All mutations to the whiteboard document are represented as **Commands** — immutable, serializable value types. The document state at any point in time is the result of applying a sequence of commands to an empty document. This enables:

- **Undo/redo** for free (replay without the last N commands).
- **Collaborative editing** (CRDT-based merge of command streams).
- **Deterministic testing** (replay a saved command log to reproduce any bug).
- **Time-travel debugging** in development builds.

### Rendering Separation

The engine maintains a **Scene Graph** that is independent of the rendering backend. The default renderer uses **Skia** (the same library used by Chrome, Flutter, and Android). A renderer is a plugin that consumes the scene graph — swapping renderers (e.g., to a Vulkan path or a PDF export renderer) does not change any engine logic.

---

## 4. Why C++?

The choice of C++ as the engine language was made deliberately and is not subject to revision without a full architectural review. The rationale:

### Performance Characteristics We Require

| Requirement | C++ | Rust | Java/Kotlin | Go | Swift |
|---|---|---|---|---|---|
| Zero-cost abstractions | YES | YES | NO | NO | Partial |
| No garbage collector pauses | YES | YES | NO | NO | YES |
| Deterministic destructors (RAII) | YES | YES | NO | NO | YES |
| Inline SIMD intrinsics | YES | YES | NO | NO | Partial |
| Direct Skia integration (C++ API) | YES | Bindings | NO | NO | Bindings |
| Stable C ABI for plugins/JNI | YES | YES | NO | YES | NO |
| NDK support (Android) | YES | YES | N/A | NO | NO |
| Mature ecosystem (CMake, vcpkg) | YES | Growing | N/A | N/A | N/A |

### GC Pause Concern

On a 90 Hz display, each frame budget is **11.1 ms**. A single JVM GC pause (even a minor G1 collection) can steal 5-30 ms. For stroke rendering, this manifests as a visible jitter that destroys the "pen on paper" feeling. C++ with a custom arena allocator for per-stroke data eliminates this class of problem entirely.

### Standard Used

The engine targets **C++20** with the following compiler minimums:
- Clang 16+ (Android NDK r26+)
- MSVC 19.35+ (Visual Studio 2022 17.5+)
- GCC 13+ (Linux CI)

C++20 features actively used: `std::span`, `std::expected`, concepts, designated initializers, `[[nodiscard]]`, coroutines (IO thread only).

---

## 5. Platform Strategy

### Android First

Android is the **primary development target** because:
1. The largest global market for whiteboard/annotation apps on touch devices.
2. NDK + JNI provides a well-defined, stable C-ABI bridge.
3. The constraint of a mobile GPU (Mali, Adreno) forces performance discipline that benefits all platforms.
4. Testing on a wide range of Android device tiers catches performance regressions early.

**Android-specific shell responsibilities:**
- Kotlin `Activity` that creates a `Surface` / `SurfaceView`.
- Touch/Stylus event translation from `MotionEvent` to `ob::InputEvent`.
- JNI bridge (`libopenboard_jni.so`) that calls into `libOpenBoard.so`.
- File access via `ContentResolver`, translated to engine-level `FileProvider` calls.

### Windows Next

Windows is the second priority for:
- Large-display / classroom scenarios with mouse + stylus (Surface Pen, Wacom).
- Native Win32/WinUI3 shell consuming the same engine DLL.
- Desktop features: multi-window, drag-and-drop, high-DPI scaling.

**Windows-specific shell responsibilities:**
- WinUI3 `SwapChainPanel` or Win32 `HWND` for the render surface.
- WM_POINTER message translation to `ob::InputEvent`.
- DLL loading, COM initialization.

### Shared Engine, Zero Duplication

```
                  +--------------------------------------+
                  |        libOpenBoard (C++20)          |
                  |  Canvas . Tools . Layers . Rendering |
                  |  History . Plugins . FileIO . Input  |
                  +-------------+----------+------------+
                                |          |
               +----------------v--+  +---v--------------------+
               |  Android Shell    |  |   Windows Shell        |
               |  (Kotlin/JNI)     |  |   (C++/WinUI3)         |
               +-------------------+  +------------------------+
```

The engine exposes a **pure C API** (`openboard_c.h`) so that any language with C FFI support can host it.

---

## 6. Key Differentiators

### Speed — The 4 ms Stroke Commitment

The engine commits to a stroke pipeline where the time from stylus ACTION_DOWN / ACTION_MOVE to a pixel appearing on screen is <= 4 ms on reference hardware. This is achieved through:

1. **Predicted stroke points** — Android's `MotionEvent.getHistoricalSize()` plus our own Kalman-filter predictor adds lookahead points before the frame deadline.
2. **GPU-resident stroke buffer** — Active strokes are stored in a persistent GPU vertex buffer (updated via `glBufferSubData` or Vulkan staging). No CPU-to-GPU upload per frame for completed strokes.
3. **Dirty-region rendering** — Only the bounding box of changed strokes is re-rendered. Unchanged regions are composited from the previous frame's texture.
4. **Front-buffer rendering** (optional, Android API 33+) — Bypasses the compositor entirely for lowest-latency stylus response.

### Plugin System

OpenBoard uses a **binary plugin ABI** versioned with semantic versioning. Plugins are shared libraries (`.so` / `.dll`) that implement a C-ABI interface defined in `sdk/include/openboard_plugin/`. Plugins can:

- Register new tools (brush types, shape generators, text tools).
- Register new file format importers/exporters.
- Add UI panels (panels are described by a data model, rendered by the shell).
- Hook into the event bus for analytics or collaboration.

Plugins are **sandboxed by convention**: they receive only opaque handles, never raw pointers to engine internals.

### Offline-First

OpenBoard requires zero network connectivity for all core features. Documents are stored in a local format (`.obdoc` — a ZIP64 archive containing a protobuf command log plus asset blobs). Cloud sync, if offered by a product built on OpenBoard, is an optional plugin that operates on the same command log (append-only, CRDT-mergeable).

---

## 7. Three-Layer Architecture

```
+======================================================================================+
|  LAYER 1: PLATFORM UI (Platform-specific, thin)                                      |
|                                                                                      |
|  Android                        Windows                  (future)                    |
|  +----------------------+       +----------------------+                             |
|  | Kotlin Activity      |       | WinUI3 / Win32 App   |                             |
|  | SurfaceView          |       | SwapChainPanel        |                             |
|  | MotionEvent handler  |       | WM_POINTER handler    |                             |
|  | File picker, IAP     |       | DnD, multi-window    |                             |
|  +----------+-----------+       +----------+-----------+                             |
+======================+==============================+===============================+
                       | JNI (Android)                | Direct C++ / C FFI (Windows)  |
                       | C ABI: openboard_c.h         | C ABI: openboard_c.h          |
+======================+==============================+===============================+
|  LAYER 2: JNI / FFI BRIDGE                                                           |
|                                                                                      |
|  Android                        Windows                                              |
|  +----------------------+       +----------------------+                             |
|  | libopenboard_jni.so  |       | openboard_c.h        |                             |
|  | ob_jni_engine.cpp    |       | (consumed directly   |                             |
|  | ob_jni_input.cpp     |       |  by Win32 C++ shell) |                             |
|  | ob_jni_document.cpp  |       +----------------------+                             |
|  +----------+-----------+                                                            |
+======================================================================================+
                       | Links to
+======================================================================================+
|  LAYER 3: C++ ENGINE (libOpenBoard.so / OpenBoard.dll)                               |
|                                                                                      |
|  +----------+ +----------+ +----------+ +----------+ +----------+                   |
|  | Canvas   | |  Tools   | |  Layers  | | History  | |  Input   |                   |
|  | Document | | ToolMgr  | |LayerStack| |HistoryMgr| |Dispatcher|                   |
|  +----+-----+ +----+-----+ +----+-----+ +----+-----+ +----+-----+                   |
|       +------------+------------+------------+-----------+                           |
|                               Scene                                                  |
|  +----------+ +----------+ +----------+ +----------+                                |
|  | Renderer | |  FileIO  | | Plugins  | |  Events  |                                |
|  |  (Skia)  | |  (.obdoc)| | PluginMgr| | EventBus |                                |
|  +----------+ +----------+ +----------+ +----------+                                |
+======================================================================================+
              |                                    |
    +---------v-----------+         +--------------v------------+
    |  Rendering Backend  |         |   Storage Backend          |
    |  Skia (GL/Vulkan)   |         |   Local FS (.obdoc)        |
    |  PDF Export         |         |   (Cloud via plugin)       |
    |  PNG/SVG Export     |         +---------------------------+
    +---------------------+
```

---

## 8. Module Dependency Diagram

The following diagram shows allowed dependency directions. **No reverse dependencies are permitted.** Enforcement is via CMake `target_link_libraries` and a CI lint step.

```
                    +-----------------------------+
                    |        ob::Plugin            |
                    |  (depends on SDK ABI only)   |
                    +-------------+---------------+
                                  | calls
+-------------------------------------v---------------------------------------+
|                         ob::PluginManager                                   |
+-----------------------------------------------------------------------------+
                 ^                                    ^
                 |                                    |
     +-----------+----------+        +----------------+----------+
     |    ob::ToolManager   |        |     ob::FileIO            |
     +-----------+----------+        +----------------+----------+
                 |                                    |
     +-----------v------------------------------------v----------+
     |                    ob::Document                           |
     |          (Scene . LayerStack . HistoryManager)            |
     +---------------------------+-------------------------------+
                                 |
              +------------------+------------------+
              v                  v                  v
      +---------------+  +--------------+  +-----------------+
      | ob::Renderer  |  | ob::EventBus |  |ob::InputDispatch |
      +---------------+  +--------------+  +-----------------+
              |                                    |
              v                                    v
      +---------------+                 +--------------------+
      |  Skia / GL    |                 |  ob::InputPredict  |
      |  Vulkan (opt) |                 |  (Kalman filter)   |
      +---------------+                 +--------------------+
              ^
              |
      +---------------+
      | ob::Allocator |  <- Arena + Pool allocators (no dependencies)
      | ob::Math      |  <- Vec2, Mat3, BezierPath   (no dependencies)
      | ob::Platform  |  <- Time, File, Thread       (no dependencies)
      +---------------+
```

**Dependency rules:**
- `ob::Math` and `ob::Platform` have **zero** engine dependencies. They may depend only on the C++ standard library.
- `ob::Renderer` depends on `ob::Document` (read-only scene access) but the scene does NOT depend on `ob::Renderer`.
- `ob::Plugin` implementations depend only on `openboard_plugin/plugin_api.h`, never on internal headers.

---

## 9. Non-Goals

The following are explicitly **not** part of the OpenBoard engine scope. If a feature request falls into this category, it belongs in a product-layer plugin, not the core engine.

| Non-Goal | Rationale |
|---|---|
| **Built-in cloud sync** | Cloud is a product concern. Engine ships with an `ISyncProvider` plugin interface only. |
| **User authentication / accounts** | Auth is a platform/product concern. Engine has no concept of a user. |
| **In-app purchase / licensing** | Platform concern. Engine does not check licenses or phone home. |
| **Chat / messaging features** | Out of scope. A collaboration plugin can bridge an external chat SDK. |
| **Video conferencing integration** | Platform / plugin concern. Engine outputs frames; consuming them over WebRTC is external. |
| **Platform-native UI widgets** | The engine does not render buttons, dialogs, or toolbars. The shell does. |
| **Web/WASM build** | Not blocked, but not a current target. The C API is WASM-compatible in principle. |
| **Scripting language embedding** | Lua/Python bindings are a plugin concern, not a core engine concern. |
| **AI/ML inference** | A plugin may call a local model. The engine does not bundle inference runtimes. |

---

## 10. Open Source Strategy

### License

The engine core (`core/`) is released under the **Apache 2.0 License**. This was chosen over GPL/LGPL to allow commercial products to ship the engine in proprietary apps without being forced to open-source their UI layer.

The plugin SDK (`sdk/`) is also Apache 2.0, ensuring plugin authors have maximum freedom.

### What Is Open

| Component | License | Open? |
|---|---|---|
| `core/` — C++ engine | Apache 2.0 | YES |
| `sdk/` — Plugin SDK headers | Apache 2.0 | YES |
| `android/` — Reference Android shell | Apache 2.0 | YES |
| `windows/` — Reference Windows shell | Apache 2.0 | YES |
| `plugins/plugin_qr_share/` | Apache 2.0 | YES |
| Official cloud sync plugin | Proprietary | NO (product layer) |

### Contribution Policy

- All contributions to `core/` require a **Contributor License Agreement (CLA)** to ensure the project can be dual-licensed commercially if needed.
- The public C API (`openboard_c.h`) is subject to a **deprecation policy**: no breaking change without a major version bump and a 12-month deprecation window.
- All public API additions must be accompanied by documentation and a unit test before merge.

### Versioning

The engine follows **Semantic Versioning 2.0.0**:
- **MAJOR** — breaking change to the public C ABI or `.obdoc` file format.
- **MINOR** — new API, backward-compatible.
- **PATCH** — bug fix, no API change.

The plugin ABI has its own version tracked separately via `OB_PLUGIN_ABI_VERSION` in `plugin_api.h`.

---

*Next: [01 - Repository Structure](./01_REPOSITORY_STRUCTURE.md)*
