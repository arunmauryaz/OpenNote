# ⚡ OpenBoard Core Engine (`libopenwhiteboard`)

> **Platform-Agnostic, Zero-Latency C++20 Digital Whiteboard Engine**  
> Designed for high-framerate inking (60/120 FPS), multi-page presentations, vector geometry, and instant undo/redo.

---

## 🎯 Architectural Philosophy

The OpenBoard Core Engine is **100% decoupled from any platform UI framework**. It contains **zero** dependencies on Android SDK, Java/Kotlin runtime, Windows Win32 UI, or desktop windowing systems.

```
┌────────────────────────────────────────────────────────────┐
│                    Your Platform Shell                     │
│  (Kotlin View / WinUI3 / Qt / GTK / Swift / WebAssembly)   │
└─────────────────────────────┬──────────────────────────────┘
                              │ Input Events & Render Calls
┌─────────────────────────────▼──────────────────────────────┐
│                    OpenBoard Core Engine                   │
│ ┌───────────────────────┐ ┌──────────────────────────────┐ │
│ │  WhiteboardEngine     │ │   Document (Pages / Layers)  │ │
│ └───────────┬───────────┘ └──────────────┬───────────────┘ │
│             │                            │                 │
│ ┌───────────▼───────────┐ ┌──────────────▼───────────────┐ │
│ │   InputDispatcher     │ │   CommandStack (Undo/Redo)   │ │
│ └───────────┬───────────┘ └──────────────┬───────────────┘ │
│             │                            │                 │
│ ┌───────────▼───────────┐ ┌──────────────▼───────────────┐ │
│ │   Tools Subsystem     │ │   AutoSave & FileFormat      │ │
│ └───────────────────────┘ └──────────────────────────────┘ │
└─────────────────────────────┬──────────────────────────────┘
                              │ Abstract Render Commands
┌─────────────────────────────▼──────────────────────────────┐
│            IRenderPipeline (GLES / Vulkan / Metal)         │
└────────────────────────────────────────────────────────────┘
```

---

## 📦 Directory Structure

```
core/
├── CMakeLists.txt             ← Cross-platform build script (Android + Desktop)
├── README.md                  ← This developer guide
├── include/
│   └── ob/                    ← Public header contracts
│       ├── WhiteboardEngine.h ← Primary engine coordinator
│       ├── Document.h         ← Document state, pages, objects
│       ├── Tools.h            ← Drawing instruments (Pen, Brush, Eraser, Shapes)
│       ├── CommandStack.h     ← Undo/redo transaction history
│       ├── CanvasCamera.h     ← Viewport transforms, zoom (0.1x - 10x), pan
│       ├── InputSystem.h      ← Touch pointers, Kalman filter, palm rejector
│       ├── IRenderPipeline.h  ← Abstract rendering interface
│       ├── RenderPipeline.h   ← GLES 3.0 implementation
│       ├── FileFormat.h       ← Native .obn binary delta serialization
│       └── AutoSaveManager.h  ← Crash journal & auto-recovery
└── src/
    ├── engine/                ← Engine core lifecycle & state
    ├── canvas/                ← Camera math and viewport culling
    ├── file/                  ← Compact binary serializer
    ├── input/                 ← Input gesture & smoothing pipeline
    ├── rendering/             ← Shader programs & GPU vertex batching
    ├── tools/                 ← Geometric math & Bézier spline generators
    └── jni/                   ← Android JNI bridge (optional)
```

---

## 🚀 Quick C++ Embedding Example

Here is how you can embed and drive the engine from any C++ application:

```cpp
#include "ob/WhiteboardEngine.h"
#include "ob/EngineConfig.h"
#include "ob/InputSystem.h"

int main() {
    // 1. Configure the engine
    ob::EngineConfig config;
    config.width = 1920;
    config.height = 1080;
    config.defaultPageWidth = 1920;
    config.defaultPageHeight = 1080;
    config.enablePalmRejection = true;

    // 2. Instantiate the engine
    auto engine = ob::WhiteboardEngine::create(config);

    // 3. Initialize your render pipeline (e.g. OpenGL ES 3.0)
    engine->initRenderer();

    // 4. Select active tool (e.g., Pen with 4.0f width and royal blue color)
    engine->tools()->setActiveTool(ob::ActiveTool::PEN);
    engine->tools()->penTool()->setColor(0xFF1E88E5);
    engine->tools()->penTool()->setWidth(4.0f);

    // 5. Feed touch / stylus input events
    ob::TouchPointer downEvent{
        .pointerId = 1,
        .x = 250.0f,
        .y = 400.0f,
        .pressure = 0.8f,
        .isPen = true
    };
    engine->input()->dispatchTouchBegan(downEvent);

    ob::TouchPointer moveEvent{
        .pointerId = 1,
        .x = 265.0f,
        .y = 415.0f,
        .pressure = 0.85f,
        .isPen = true
    };
    engine->input()->dispatchTouchMoved(moveEvent);

    engine->input()->dispatchTouchEnded(moveEvent);

    // 6. Trigger a frame render
    engine->renderFrame();

    // 7. Undo / Redo is built-in
    engine->commands()->undo();
    engine->commands()->redo();

    return 0;
}
```

---

## 🛠️ Building the Standalone Core

### On Windows / Linux / macOS (Desktop)
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```
This generates `libopenwhiteboard_core.a` (or `openwhiteboard_core.lib`) and `openwhiteboard_shared` for direct linking.

### In CMake Subprojects
Add the engine directory directly to your own `CMakeLists.txt`:
```cmake
add_subdirectory(core)
target_link_libraries(my_desktop_app PRIVATE openwhiteboard_core)
```

---

## 📜 License
OpenBoard Core Engine — MIT License.
