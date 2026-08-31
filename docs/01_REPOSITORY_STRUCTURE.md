# 01 — OpenBoard: Repository Structure

> **Document version:** 1.0.0
> **Last updated:** 2026-08-04
> **Audience:** All contributors. Read this before touching any file in the repo.

---

## Table of Contents

1. [Top-Level Layout](#1-top-level-layout)
2. [core/ — C++ Engine](#2-core--c-engine)
3. [android/ — Android Shell](#3-android--android-shell)
4. [windows/ — Windows Shell](#4-windows--windows-shell)
5. [plugins/ — Official Plugins](#5-plugins--official-plugins)
6. [sdk/ — Plugin SDK](#6-sdk--plugin-sdk)
7. [tools/ — Build and Dev Scripts](#7-tools--build-and-dev-scripts)
8. [docs/ — Documentation](#8-docs--documentation)
9. [third_party/ — Vendored Libraries](#9-third_party--vendored-libraries)
10. [CMakeLists.txt Structure](#10-cmakeliststxt-structure)
11. [Gradle Integration Notes](#11-gradle-integration-notes)
12. [NDK Version Requirements](#12-ndk-version-requirements)
13. [Third-Party Vendoring Policy](#13-third-party-vendoring-policy)
14. [CI/CD Pipeline Overview](#14-cicd-pipeline-overview)

---

## 1. Top-Level Layout

```
openboard/
|-- core/                        C++ engine source (the heart of everything)
|   |-- include/openboard/       Public headers (installed with SDK)
|   |-- src/                     Implementation files
|   |   |-- canvas/              Document model and canvas state
|   |   |-- tools/               Tool implementations (pen, eraser, shapes...)
|   |   |-- layers/              Layer stack management
|   |   |-- rendering/           Skia-based renderer, scene graph
|   |   |-- input/               Input event pipeline and prediction
|   |   |-- fileio/              .obdoc read/write, asset management
|   |   |-- undo/                Command objects, history stack
|   |   |-- plugins/             Plugin loader, ABI bridge, sandbox
|   |   |-- events/              Typed event bus
|   |   |-- math/                Vec2, Mat3, Rect, BezierPath
|   |   |-- platform/            OS abstraction (threads, time, file)
|   |   `-- allocator/           Arena and pool allocators
|   |-- tests/                   GoogleTest unit + integration tests
|   |-- benchmarks/              Google Benchmark microbenchmarks
|   `-- CMakeLists.txt           Engine build definition
|
|-- android/                     Android application shell (Kotlin)
|   |-- app/
|   |   `-- src/main/
|   |       |-- cpp/
|   |       |   `-- jni/         JNI bridge C++ source
|   |       |-- kotlin/          Kotlin UI layer
|   |       |   |-- ui/          Activities, Fragments, ViewModels
|   |       |   |-- bridge/      Kotlin-side JNI wrappers
|   |       |   `-- util/        Android utilities
|   |       `-- res/             Layouts, drawables, strings
|   |-- build.gradle             App-level Gradle config
|   `-- CMakeLists.txt           NDK build config (references core/)
|
|-- windows/                     Windows application shell
|   |-- src/
|   |   |-- main.cpp             Entry point, WinMain
|   |   |-- window/              Win32 / WinUI3 window management
|   |   |-- bridge/              C FFI calls into engine DLL
|   |   `-- util/                Windows utilities (HiDPI, registry)
|   |-- resources/               RCDATA, icons, manifests
|   `-- CMakeLists.txt           Windows build definition
|
|-- plugins/                     Official first-party plugins
|   |-- plugin_qr_share/         QR code share plugin
|   |-- plugin_3d_shapes/        3D shape import plugin
|   `-- plugin_pdf_export/       PDF export plugin
|
|-- sdk/                         Plugin development kit
|   |-- include/
|   |   `-- openboard_plugin/    Public plugin API headers
|   |       |-- plugin_api.h     Core plugin ABI definition
|   |       |-- tool_api.h       Tool plugin interface
|   |       |-- io_api.h         Import/Export plugin interface
|   |       `-- event_api.h      Event hook interface
|   |-- examples/                Example plugin implementations
|   `-- CMakeLists.txt           SDK install target
|
|-- tools/                       Build and developer tooling
|   |-- build_android.sh         One-shot Android build script
|   |-- build_windows.ps1        One-shot Windows build script
|   |-- gen_jni_bindings.py      Auto-generate JNI wrapper boilerplate
|   |-- check_abi.sh             Verify plugin ABI compatibility
|   |-- format.sh                clang-format + ktlint run
|   `-- perf/                    Performance regression scripts
|
|-- docs/                        Documentation (you are here)
|   |-- 00_PROJECT_OVERVIEW.md
|   |-- 01_REPOSITORY_STRUCTURE.md
|   |-- 02_CORE_ENGINE.md
|   |-- 03_JNI_BRIDGE.md
|   |-- 04_PLUGIN_SYSTEM.md
|   |-- 05_RENDERING_PIPELINE.md
|   |-- 06_INPUT_PIPELINE.md
|   |-- 07_FILE_FORMAT.md
|   |-- 08_TESTING_STRATEGY.md
|   `-- adr/                     Architecture Decision Records
|
`-- third_party/                 Vendored external libraries
    |-- skia/                    Graphics library (pinned commit)
    |-- pdfium/                  PDF rendering (pinned commit)
    |-- googletest/              Unit test framework
    |-- googlebenchmark/         Microbenchmark framework
    |-- protobuf/                Serialization (.obdoc format)
    |-- zlib/                    ZIP64 compression (.obdoc container)
    `-- CMakeLists.txt           Aggregate third_party build
```

---

## 2. core/ — C++ Engine

This is the **only folder** that matters for cross-platform behavior. Every other folder is a platform adapter or tooling. New features begin here, not in `android/` or `windows/`.

### core/include/openboard/

Public installed headers — the engine's API surface. These headers:

- Are the only headers that external code (JNI bridge, Windows shell, plugins) may include.
- Use only C++20 standard-library types and engine-defined types in their signatures.
- Are installed by CMake's `install(TARGETS openboard PUBLIC_HEADER DESTINATION include/openboard)`.
- Must maintain backward compatibility within a MINOR version.

**Key public headers:**

| Header | Purpose |
|---|---|
| `engine.h` | `WhiteboardEngine` class — top-level engine handle |
| `document.h` | `Document`, `Scene` — the whiteboard data model |
| `tool_manager.h` | `ToolManager` — active tool query/switch |
| `layer.h` | `Layer`, `LayerStack` — layer model |
| `input_event.h` | `InputEvent`, `InputPoint` — input data structures |
| `history.h` | `HistoryManager` — undo/redo interface |
| `plugin_manager.h` | `PluginManager` — load/unload plugins |
| `renderer.h` | `IRenderer` — abstract renderer interface |
| `result.h` | `Result<T, Error>` — error handling type |
| `config.h` | `EngineConfig` — initialization configuration |
| `openboard_c.h` | Pure C API for JNI and FFI consumers |

### core/src/canvas/

Owns the **Document** and **Scene** objects. The document is the top-level container for all whiteboard content. The scene is the in-memory, render-ready representation of that content.

Key files:

```
canvas/
|-- document.cpp              Document load/save, command application
|-- scene.cpp                 Scene graph construction from document state
|-- stroke.cpp                Stroke data structure, bezier fitting
|-- stroke_builder.cpp        Incremental stroke construction from input points
|-- canvas_transform.cpp      Pan/zoom transform state
`-- selection.cpp             Selection rectangle, multi-object selection
```

**Invariants enforced here:**
- A `Document` is always in a valid state after every `applyCommand()` call.
- A `Scene` is always a pure function of the `Document` state — no hidden mutable state.
- `Stroke` objects are immutable after construction. Modification creates a new stroke via command.

### core/src/tools/

Each tool is a stateless (or minimally-stateful) object that converts `InputEvent` sequences into `Command` objects. Tools do NOT modify the document directly; they emit commands that are applied by the engine.

```
tools/
|-- tool_base.cpp             Abstract base, shared tool lifecycle
|-- pen_tool.cpp              Freehand pen: collects points, fits bezier on lift
|-- eraser_tool.cpp           Erase by stroke intersection or pixel mask
|-- highlighter_tool.cpp      Pen variant with multiply blend mode
|-- shape_tool.cpp            Rectangle, ellipse, line with snap-to-grid
|-- text_tool.cpp             Text box creation and editing
|-- selector_tool.cpp         Lasso and rectangle selection
|-- pan_tool.cpp              Canvas pan (two-finger drag)
|-- zoom_tool.cpp             Pinch-to-zoom (handled in InputDispatcher)
`-- tool_registry.cpp         Maps tool IDs to factory functions (plugin hook point)
```

### core/src/layers/

Manages the ordered stack of layers in a document. Layers have: visibility, lock state, opacity, blend mode, and a name.

```
layers/
|-- layer_stack.cpp           Ordered list of layers, active layer tracking
|-- layer.cpp                 Individual layer: metadata + stroke list
|-- layer_commands.cpp        AddLayer, RemoveLayer, ReorderLayer, SetLayerProp commands
`-- layer_compositor.cpp      Flattens layer stack into a single scene for rendering
```

### core/src/rendering/

Contains the Skia-based renderer and all rendering utilities.

```
rendering/
|-- skia_renderer.cpp         IRenderer implementation using Skia
|-- skia_stroke_renderer.cpp  Converts Stroke bezier paths to Skia SkPath
|-- skia_text_renderer.cpp    Text layout via Skia + HarfBuzz (optional)
|-- dirty_region_tracker.cpp  Tracks which screen regions need redrawing
|-- render_frame.cpp          RenderFrame struct: snapshot passed to render thread
|-- gpu_stroke_buffer.cpp     GPU-resident vertex buffer for active stroke
`-- renderer_factory.cpp      Creates renderer based on EngineConfig
```

### core/src/input/

The input pipeline takes raw platform events and produces high-quality, predicted input points for the active tool.

```
input/
|-- input_dispatcher.cpp      Routes events to tools or gesture recognizers
|-- input_predictor.cpp       Kalman filter stroke predictor
|-- gesture_recognizer.cpp    Two-finger pan, pinch, rotate detection
|-- stylus_calibration.cpp    Per-device pressure curve calibration
`-- input_coalescer.cpp       Coalesces batched MotionEvent historical points
```

### core/src/fileio/

Handles reading and writing the `.obdoc` document format (ZIP64 + protobuf command log + asset blobs).

```
fileio/
|-- obdoc_writer.cpp          Serializes Document to .obdoc
|-- obdoc_reader.cpp          Deserializes .obdoc to Document
|-- asset_store.cpp           Manages embedded images/PDFs within .obdoc
|-- format_migrator.cpp       Upgrades old .obdoc versions to current schema
|-- pdf_importer.cpp          Imports PDF pages as background layers (via PDFium)
|-- image_importer.cpp        Imports PNG/JPEG as image strokes
`-- export_png.cpp            Exports canvas region to PNG
```

### core/src/undo/

The undo system is pure command sourcing. No special "undo logic" exists in tools — every tool emits reversible Commands.

```
undo/
|-- history_manager.cpp       Push, undo, redo, cap at configurable limit
|-- command.h                 Abstract Command interface (apply / unapply)
|-- command_group.cpp         Groups multiple commands into one undo step
|-- stroke_commands.cpp       AddStroke, RemoveStroke, ModifyStroke
|-- layer_commands.cpp        (see layers/ above, shared header)
|-- transform_commands.cpp    TranslateSelection, ScaleSelection, RotateSelection
`-- property_commands.cpp     ChangeToolProperty, ChangeLayerProperty
```

### core/src/plugins/

Plugin loader, ABI adapter, and lifecycle management.

```
plugins/
|-- plugin_manager.cpp        dlopen/LoadLibrary, version check, lifecycle
|-- plugin_abi_adapter.cpp    Wraps C-ABI plugin calls in C++ interface
|-- plugin_sandbox.cpp        Memory guard page setup for plugin heap
|-- tool_plugin_host.cpp      Hosts tool plugins, integrates with ToolManager
|-- io_plugin_host.cpp        Hosts importer/exporter plugins
`-- event_plugin_host.cpp     Delivers EventBus events to plugin subscribers
```

### core/tests/

All tests use **GoogleTest**. Test organization mirrors `src/`:

```
tests/
|-- canvas/
|   |-- document_test.cpp         Command application, invariant checks
|   `-- stroke_test.cpp           Bezier fitting accuracy
|-- tools/
|   |-- pen_tool_test.cpp         Pen stroke construction
|   `-- eraser_tool_test.cpp      Erase intersection math
|-- rendering/
|   `-- dirty_region_test.cpp     Dirty rect union/intersection
|-- input/
|   `-- predictor_test.cpp        Kalman filter convergence
|-- fileio/
|   `-- obdoc_roundtrip_test.cpp  Write then read, compare documents
|-- undo/
|   `-- history_test.cpp          Push/undo/redo sequence tests
|-- integration/
|   `-- full_stroke_test.cpp      End-to-end: input -> tool -> command -> document -> render
`-- CMakeLists.txt
```

**Test naming convention:** `<module>_<class>_<scenario>_<expected result>`
Example: `PenTool_SingleStroke_LiftPen_ProducesValidBezier`

### core/benchmarks/

Google Benchmark targets for performance-critical paths:

```
benchmarks/
|-- stroke_render_bench.cpp   Render N strokes, measure frame time
|-- input_predictor_bench.cpp Kalman filter throughput
|-- obdoc_write_bench.cpp     Serialize large document
`-- CMakeLists.txt
```

---

## 3. android/ — Android Shell

The Android shell is a **Kotlin application** that creates a rendering surface and forwards events to the engine. It contains almost no business logic.

### android/app/src/main/cpp/jni/

JNI bridge — thin C++ wrappers that translate between Java types and engine C++ types.

```
jni/
|-- ob_jni_engine.cpp         Java_com_openboard_Engine_*: engine lifecycle
|-- ob_jni_document.cpp       Java_com_openboard_Document_*: document ops
|-- ob_jni_input.cpp          Java_com_openboard_Input_*: MotionEvent -> InputEvent
|-- ob_jni_renderer.cpp       Java_com_openboard_Renderer_*: surface attach/detach
`-- ob_jni_plugin.cpp         Java_com_openboard_Plugin_*: plugin load/unload
```

**JNI naming convention:** `Java_<reverse_domain>_<ClassName>_<methodName>`

All JNI functions return primitive types or `jlong` (opaque handle) only. Complex data is never passed as Java objects across the JNI boundary — this avoids JNI overhead from object field access.

### android/app/src/main/kotlin/

```
kotlin/
|-- ui/
|   |-- MainActivity.kt          Single-activity app entry point
|   |-- WhiteboardFragment.kt    SurfaceView host, forwards MotionEvents
|   |-- ToolbarFragment.kt       Tool selection UI (emits tool changes to engine)
|   `-- LayerPanelFragment.kt    Layer management UI (reads engine state snapshots)
|-- bridge/
|   |-- EngineHandle.kt          Kotlin wrapper around JNI engine handle (jlong)
|   |-- DocumentSnapshot.kt      Immutable data class for UI state rendering
|   `-- EventObserver.kt         Observes engine event callbacks on main thread
`-- util/
    |-- MotionEventConverter.kt  MotionEvent -> InputEvent data class
    `-- SurfaceHelper.kt         Surface lifecycle management
```

### android/app/src/main/res/

Standard Android resource structure. No business logic. Layouts are minimal — the whiteboard surface is a `SurfaceView` that takes up the full screen; all toolbar UI is overlaid using `ConstraintLayout` with translucent backgrounds.

### android/CMakeLists.txt (NDK build config)

```cmake
cmake_minimum_required(VERSION 3.22)
project(openboard_android)

# Include the core engine as a subdirectory
add_subdirectory(../../core ${CMAKE_CURRENT_BINARY_DIR}/core)

# JNI bridge shared library
add_library(openboard_jni SHARED
    app/src/main/cpp/jni/ob_jni_engine.cpp
    app/src/main/cpp/jni/ob_jni_document.cpp
    app/src/main/cpp/jni/ob_jni_input.cpp
    app/src/main/cpp/jni/ob_jni_renderer.cpp
    app/src/main/cpp/jni/ob_jni_plugin.cpp
)

target_link_libraries(openboard_jni
    PRIVATE openboard   # The engine
    PRIVATE android     # Android NDK
    PRIVATE log         # Android logcat
    PRIVATE EGL         # OpenGL ES context
    PRIVATE GLESv3      # OpenGL ES 3.2
)
```

---

## 4. windows/ — Windows Shell

The Windows shell is a **C++ Win32/WinUI3 application** that is simpler than the Android shell because it can call the engine directly without JNI.

```
windows/src/
|-- main.cpp                     WinMain, COM init, message loop
|-- window/
|   |-- MainWindow.cpp           Win32 HWND or WinUI3 Window wrapper
|   |-- SwapChainSurface.cpp     DXGI SwapChain for GPU surface
|   `-- PointerInputHandler.cpp  WM_POINTER -> ob::InputEvent
|-- bridge/
|   |-- EngineWrapper.cpp        C++ wrapper around openboard_c.h API
|   `-- DocumentView.cpp         Reads engine snapshots, drives UI
`-- util/
    |-- HiDPIHelper.cpp          DPI scaling utilities
    `-- FileDialogHelper.cpp     IFileOpenDialog wrappers
```

The Windows shell links against `OpenBoard.dll` (the engine) and `openboard_c.h` (the C API). No JNI layer is needed — Win32 C++ can call C/C++ APIs natively.

---

## 5. plugins/ — Official Plugins

Each plugin is a self-contained CMake project that links only against the plugin SDK (`sdk/`). Plugins must NOT link against `core/` internals.

### plugin_qr_share/

Generates a QR code containing a shareable link to the current document snapshot (if a backend URL is configured). Implemented as an `IExportPlugin`.

```
plugin_qr_share/
|-- src/
|   |-- qr_export_plugin.cpp  Implements ob_plugin_io_t export interface
|   `-- qr_generator.cpp      QR encoding via libqrencode (bundled)
|-- third_party/libqrencode/
`-- CMakeLists.txt
```

### plugin_3d_shapes/

Imports 3D model files (`.obj`, `.gltf`) and renders them as flat SVG-like vector outlines on the canvas. Implemented as an `IImportPlugin` + `IToolPlugin`.

```
plugin_3d_shapes/
|-- src/
|   |-- shape_import_plugin.cpp
|   |-- shape_tool_plugin.cpp
|   `-- obj_parser.cpp
`-- CMakeLists.txt
```

### plugin_pdf_export/

Exports the document as a multi-page PDF using PDFium's write API.

```
plugin_pdf_export/
|-- src/
|   `-- pdf_export_plugin.cpp  Implements ob_plugin_io_t export interface
`-- CMakeLists.txt
```

---

## 6. sdk/ — Plugin SDK

The plugin SDK is what third-party developers receive to build plugins.

### sdk/include/openboard_plugin/

```cpp
// plugin_api.h — Core plugin ABI entry points

#define OB_PLUGIN_ABI_VERSION 3

// Every plugin .so/.dll must export this symbol:
extern "C" ob_plugin_desc_t* ob_plugin_get_descriptor(void);

typedef struct ob_plugin_desc_t {
    uint32_t         abi_version;  // Must equal OB_PLUGIN_ABI_VERSION
    const char*      plugin_id;    // Reverse-domain, e.g. "com.example.myplugin"
    const char*      display_name;
    const char*      version;      // Semver string
    ob_plugin_type_t type;         // TOOL | IO | EVENT | RENDERER
    void*            impl;         // ob_plugin_tool_t*, ob_plugin_io_t*, etc.
} ob_plugin_desc_t;
```

**SDK headers are the ONLY contract between plugins and the engine.** Internal engine headers are never shipped with the SDK. Breaking changes to SDK headers increment `OB_PLUGIN_ABI_VERSION` and require a major engine version bump.

### sdk/examples/

```
examples/
|-- hello_tool/     Minimal tool plugin that draws a smiley face stamp
|-- csv_exporter/   Exports stroke data as CSV (demonstrates IExportPlugin)
`-- event_logger/   Logs all engine events to a file (demonstrates IEventPlugin)
```

---

## 7. tools/ — Build and Dev Scripts

### build_android.sh

```bash
#!/usr/bin/env bash
# Usage: ./tools/build_android.sh [debug|release]
# Builds the Android APK with NDK, runs unit tests on connected device.

BUILD_TYPE=${1:-debug}
./gradlew :app:assemble${BUILD_TYPE^} \
  -PANDROID_NDK_VERSION=r26d \
  -PCMAKE_BUILD_TYPE=$BUILD_TYPE
```

### build_windows.ps1

```powershell
# Usage: .\tools\build_windows.ps1 [-Config Debug|Release]
param([string]$Config = "Debug")
cmake -B build/windows -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_BUILD_TYPE=$Config `
      -DOPENBOARD_BUILD_TESTS=ON
cmake --build build/windows --config $Config
```

### gen_jni_bindings.py

Parses `core/include/openboard/openboard_c.h` and auto-generates the Kotlin `external fun` declarations and the C++ JNI stub signatures. Keeps JNI bridge in sync with the C API automatically.

```
Usage: python tools/gen_jni_bindings.py \
           --input core/include/openboard/openboard_c.h \
           --kotlin-out android/app/src/main/kotlin/bridge/GeneratedBindings.kt \
           --cpp-out android/app/src/main/cpp/jni/ob_jni_generated.cpp
```

### check_abi.sh

Uses `abidiff` (libabigail) to compare the current `openboard_c.h` against the last released version. Fails CI if any breaking change is detected without a corresponding major version bump.

### format.sh

Runs `clang-format --style=file` on all `core/` and `sdk/` C++ files, and `ktlint` on all Kotlin files. Must pass before merge.

---

## 8. docs/ — Documentation

All documentation is Markdown. Files are numbered for reading order. Architecture Decision Records (ADRs) live in `docs/adr/` and capture the reasoning behind major decisions.

**ADR naming:** `adr/<NNN>-<short-title>.md`
Example: `adr/001-use-skia-over-nanovg.md`

---

## 9. third_party/ — Vendored Libraries

### Vendoring Policy

All third-party libraries are **fully vendored** (source included in the repository). Reasons:

1. Reproducible builds — no dependency on external package registries.
2. Controlled patching — we apply Android-specific or performance patches without waiting for upstream.
3. Air-gap builds — the engine can be built on networks with no internet access.

### Vendored Libraries

| Library | Version / Commit | Purpose | License |
|---|---|---|---|
| **Skia** | `m126-<commit>` | 2D graphics rendering | BSD 3-Clause |
| **PDFium** | `6502-<commit>` | PDF import rendering | BSD 3-Clause + Apache 2.0 |
| **GoogleTest** | `v1.14.0` | Unit test framework | BSD 3-Clause |
| **Google Benchmark** | `v1.8.3` | Microbenchmarks | Apache 2.0 |
| **protobuf** | `v26.1` | .obdoc command log serialization | BSD 3-Clause |
| **zlib** | `v1.3.1` | ZIP64 container compression | zlib License |

### Update Procedure

1. Create a branch: `deps/update-<libname>-<version>`.
2. Replace the vendored source under `third_party/<libname>/`.
3. Update the version comment in `third_party/CMakeLists.txt`.
4. Run the full test suite and benchmark suite.
5. Run `tools/check_abi.sh` — a library update must not break the engine's public ABI.
6. Open a PR with the benchmark diff attached.

### third_party/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)

# Skia — built as a static library
# Pinned to: m126-abc1234 (2024-06-15)
add_subdirectory(skia)

# PDFium — built as a static library
# Pinned to: 6502-def5678 (2024-05-20)
add_subdirectory(pdfium)

# GoogleTest — only built when OPENBOARD_BUILD_TESTS=ON
if(OPENBOARD_BUILD_TESTS)
    add_subdirectory(googletest)
    add_subdirectory(googlebenchmark)
endif()

# Protobuf — static library. Pinned to: v26.1
add_subdirectory(protobuf)

# zlib — static library. Pinned to: v1.3.1
add_subdirectory(zlib)
```

---

## 10. CMakeLists.txt Structure

### Root core/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(openboard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(OPENBOARD_BUILD_TESTS      "Build unit tests"                        OFF)
option(OPENBOARD_BUILD_BENCHMARKS "Build benchmarks"                        OFF)
option(OPENBOARD_ENABLE_VULKAN    "Enable Vulkan renderer"                  OFF)
option(OPENBOARD_PLUGIN_SANDBOX   "Enable plugin sandboxing (Linux/Android)" ON)

# Third-party dependencies
add_subdirectory(../third_party ${CMAKE_CURRENT_BINARY_DIR}/third_party)

# Collect engine sources
file(GLOB_RECURSE OPENBOARD_SOURCES
    src/canvas/*.cpp    src/tools/*.cpp     src/layers/*.cpp
    src/rendering/*.cpp src/input/*.cpp     src/fileio/*.cpp
    src/undo/*.cpp      src/plugins/*.cpp   src/events/*.cpp
    src/math/*.cpp      src/platform/*.cpp  src/allocator/*.cpp
)

# Engine shared library
add_library(openboard SHARED ${OPENBOARD_SOURCES})

target_include_directories(openboard
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(openboard PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Werror -fno-rtti>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /GR->
)

target_link_libraries(openboard
    PRIVATE skia
    PRIVATE pdfium
    PRIVATE protobuf::libprotobuf
    PRIVATE zlib
)

if(OPENBOARD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OPENBOARD_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

install(TARGETS openboard
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/openboard
)
install(DIRECTORY include/openboard DESTINATION include)
```

**Important compiler flags:**
- `-fno-rtti` / `/GR-` — RTTI is disabled engine-wide. Dynamic dispatch uses the engine's own virtual table mechanism, not `dynamic_cast`. This reduces binary size and eliminates RTTI overhead across JNI calls.
- Exceptions are NOT disabled — `std::expected` is preferred but exceptions are permitted in non-hot paths (IO, plugin load).

---

## 11. Gradle Integration Notes

### build.gradle (app level)

```groovy
android {
    compileSdk 35
    ndkVersion "26.3.11579264"    // r26d — minimum required (see section 12)

    defaultConfig {
        minSdk 26                 // Android 8.0 — OpenGL ES 3.2 required
        targetSdk 35
        ndk {
            abiFilters "arm64-v8a", "x86_64"  // x86_64 for emulator only
        }
        externalNativeBuild {
            cmake {
                cppFlags "-std=c++20"
                arguments "-DANDROID_STL=c++_shared",
                          "-DOPENBOARD_BUILD_TESTS=OFF",
                          "-DCMAKE_BUILD_TYPE=${buildType.name.capitalize()}"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path "CMakeLists.txt"
            version "3.22.1"
        }
    }

    packagingOptions {
        // All .so files must share one STL instance
        pickFirst "**/libc++_shared.so"
        // Keep debug symbols in debug builds for addr2line
        doNotStrip "**/*.so"    // overridden in release buildType
    }
}

dependencies {
    implementation "androidx.core:core-ktx:1.13.1"
    implementation "androidx.fragment:fragment-ktx:1.8.1"
    // No Jetpack Compose: SurfaceView rendering does not benefit from Compose
}
```

### STL Sharing Warning

> [!WARNING]
> All `.so` files loaded in the same process (engine + JNI bridge + plugins) **must** use the same C++ STL instance. The build enforces `ANDROID_STL=c++_shared`. A plugin compiled with a different STL will crash at runtime.

---

## 12. NDK Version Requirements

| NDK Version | Minimum | Recommended | Notes |
|---|---|---|---|
| r25 | NO | NO | Missing C++20 `std::expected` in libc++ |
| **r26d** | YES | YES | C++20 complete, Clang 17, stable |
| r27 | YES | NO | Not yet validated against full test suite |

The NDK version is pinned in two places:
1. `android/build.gradle`: `ndkVersion "26.3.11579264"`
2. `tools/build_android.sh`: `-PANDROID_NDK_VERSION=r26d`

Both must be updated together. A CI check validates that they match.

### ABI Support Matrix

| ABI | Supported | Notes |
|---|---|---|
| `arm64-v8a` | YES | Primary target. All perf work targets this ABI. |
| `armeabi-v7a` | NO | 32-bit ARM dropped. No NEON SIMD for bezier fitting. |
| `x86_64` | YES | Emulator only. Not shipped in release APK. |
| `x86` | NO | Emulator only, not worth maintaining. |

---

## 13. Third-Party Vendoring Policy

### What Gets Vendored

- Libraries that are **build-time dependencies** of the engine.
- Libraries with **custom patches** we need to maintain.
- Libraries where **version pinning is critical** for ABI stability.

### What Does NOT Get Vendored

- Android SDK / NDK / Gradle — managed by the Android Gradle Plugin.
- System libraries (`libc`, `libm`, `libdl`, `log`, `android`, `EGL`, `GLESv3`) — linked dynamically from the OS.
- Test-only tools (`adb`, `ninja`, `cmake`) — expected to be installed in CI/dev environment.

### Patch Management

Patches are stored in `third_party/<libname>/patches/<description>.patch` and applied via `execute_process` at CMake configure time. This makes patches visible, reviewable, and reproducible.

---

## 14. CI/CD Pipeline Overview

The repository uses GitHub Actions. Workflows are defined in `.github/workflows/`.

| Workflow | Trigger | What It Does |
|---|---|---|
| `ci_android.yml` | Push to `main`, any PR | Build debug + release APK, run unit tests on Android emulator |
| `ci_windows.yml` | Push to `main`, any PR | Build Debug + Release on Windows Server 2022, run unit tests |
| `ci_linux.yml` | Push to `main`, any PR | Build with GCC 13 (cross-compiler validation), run tests |
| `ci_benchmarks.yml` | Push to `main` only | Run benchmark suite, post regression report to PR |
| `ci_abi_check.yml` | Push to `main`, any PR | `abidiff` check on `openboard_c.h` against last release |
| `ci_format.yml` | Any PR | `clang-format` + `ktlint` check — fails on any diff |
| `ci_plugin_abi.yml` | Push to `main` only | Build all official plugins against current SDK, verify they load |

### Cache Strategy

- CMake build directories are cached by hash of `CMakeLists.txt` + `third_party/` contents.
- NDK download is cached by NDK version string.
- Gradle caches use the standard Gradle cache action.

---

*Previous: [00 - Project Overview](./00_PROJECT_OVERVIEW.md)*
*Next: [02 - Core Engine](./02_CORE_ENGINE.md)*
