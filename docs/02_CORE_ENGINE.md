# 02 — Core Engine Architecture & Lifecycle

## Overview

The C++ Core Engine (`libOpenBoard`) is a platform-agnostic, multi-threaded engine managing all whiteboard operations. It controls rendering, canvas transformations, tool state machines, undo/redo history stacks, layer compositing, plugin execution, and the **Auto-Save & Crash Recovery System**.

---

## ⚙️ Core Engine Configuration Struct

```cpp
namespace ob {

enum class CanvasMode {
    PageSlide,    // Fixed dimension slide pages (Default: 16:9 1920x1080)
    Infinite      // Unbounded 2D free-movement canvas
};

struct EngineConfig {
    CanvasMode defaultCanvasMode = CanvasMode::PageSlide;
    float      defaultPageWidth  = 1920.0f; // Default 16:9 Widescreen width
    float      defaultPageHeight = 1080.0f; // Default 16:9 Widescreen height
    float      defaultPageDpi    = 300.0f;
    uint32_t   defaultBackgroundColor = 0xFFFFFFFF; // White
    
    // Performance & Hardware
    bool     enableVulkan = true;
    uint32_t autoSaveIntervalSec = 30;
    size_t   maxHistoryMemoryBytes = 512 * 1024 * 1024; // 512 MB
};

// Auto-Save & Recovery Manager
class AutoSaveManager {
private:
    std::string recoveryDirPath_;
    bool isDirty_ = false;
    uint64_t lastSaveTime_ = 0;
    uint32_t autoSaveIntervalSec_ = 30;
    std::thread backgroundIoThread_;

public:
    void initialize(const std::string& dataDir);
    void markDirty();
    bool checkUncleanShutdown(std::string& outRecoveryFilePath);
    void triggerBackgroundAutoSave(const DocumentSnapshot& snapshot);
    void restoreFromRecoveryFile(const std::string& recoveryPath);
    void discardRecoveryFile();
    void markCleanExit();
};

class WhiteboardEngine {
private:
    EngineConfig                      config_;
    std::unique_ptr<Document>         document_;
    std::unique_ptr<CanvasCamera>     camera_;
    std::unique_ptr<ToolManager>      toolManager_;
    std::unique_ptr<LayerStack>       layerStack_;
    std::unique_ptr<HistoryManager>    history_;
    std::unique_ptr<PluginManager>     pluginManager_;
    std::unique_ptr<AutoSaveManager>   autoSaveManager_;

public:
    WhiteboardEngine(const EngineConfig& config);
    ~WhiteboardEngine();

    // Lifecycle
    void onSurfaceCreated(void* nativeWindow, int width, int height);
    void onSurfaceChanged(int width, int height);
    void onSurfaceDestroyed();
    void stepFrame(float deltaTime);

    // Canvas Configuration Settings API
    void setCanvasMode(CanvasMode mode);
    void setPageDimensions(float width, float height);

    // Input Dispatcher
    void processInputEvent(const InputEvent& event);

    // Crash Recovery API
    bool hasUncleanShutdown(std::string& outRecoveryPath);
    void recoverPreviousSession(const std::string& path);
    void discardPreviousSession();
    void shutdownCleanly();
};

} // namespace ob
```
