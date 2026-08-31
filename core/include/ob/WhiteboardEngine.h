#pragma once

// ─── WhiteboardEngine — Central Engine Facade ────────────────────────────────
//
// The engine core is 100% platform-agnostic. It depends only on:
//   IRenderPipeline (injected at construction, not included here)
//   Document, CanvasCamera, Tools, CommandStack, AutoSaveManager, InputDispatcher
//
// Platform bridge (JNI / Win32 / Obj-C) must:
//   1. Create the platform-specific renderer (e.g., GlesRenderPipeline)
//   2. Call WhiteboardEngine::initRenderer(renderer, window, w, h)
//   3. Drive renderFrame() each vsync on the GL thread
// ─────────────────────────────────────────────────────────────────────────────

#include "ob/Types.h"
#include "ob/Document.h"
#include "ob/EngineConfig.h"
#include "ob/InputSystem.h"
#include "ob/Command.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>

namespace ob {

// Forward declarations — engine does NOT pull in concrete platform types
class IRenderPipeline;
class CanvasCamera;
class ToolManager;
class CommandStack;
class AutoSaveManager;
class InputDispatcher;

// ─── Callbacks — platform fills these, engine calls them ────────────────────
struct EngineCallbacks {
    std::function<void()>                                           onInvalidate;
    std::function<void(bool isDirty)>                               onDocumentDirtyChanged;
    std::function<void(const std::string& path, int pages, int64_t ts)> onRecoveryAvailable;
    std::function<void(bool success)>                               onAutoSaveComplete;
    std::function<void()>                                           onPagesChanged;
    std::function<void(bool canUndo, bool canRedo)>                 onUndoRedoChanged;
    std::function<void(const std::string& message)>                 onError;
    std::function<void(bool hasSel, bool isLocked, float l, float t, float r, float b)> onSelectionChanged;
};

// ─── WhiteboardEngine ────────────────────────────────────────────────────────
class WhiteboardEngine {
public:
    // config.autoSavePath should be set before construction on Android
    explicit WhiteboardEngine(const EngineConfig& config = {});
    ~WhiteboardEngine();

    WhiteboardEngine(const WhiteboardEngine&)            = delete;
    WhiteboardEngine& operator=(const WhiteboardEngine&) = delete;

    // ── Platform Wiring ────────────────────────────────────────────────────
    // Called by the JNI bridge / platform layer before any rendering.
    // renderer: platform-created (e.g. new GlesRenderPipeline()). Engine takes
    //           ownership.
    void setCallbacks(EngineCallbacks callbacks);
    const EngineCallbacks& callbacks() const { return m_callbacks; }
    bool initRenderer(IRenderPipeline* renderer, void* nativeWindow, int32_t w, int32_t h);

    // ── Lifecycle ──────────────────────────────────────────────────────────
    void onSurfaceChanged(int32_t w, int32_t h);
    void destroyRenderer();
    void renderFrame();         // Call from render thread each vsync

    // ── Input ──────────────────────────────────────────────────────────────
    void onTouchEvent(const TouchEvent& event);

    // ── Tool API ──────────────────────────────────────────────────────────
    void setActiveTool(ToolType tool);
    void setStrokeColor(Color c);
    void setStrokeWidth(float w);
    void setStrokeOpacity(float op);
    void setPenType(uint8_t penType);
    void setEraserSize(float sz);
    void setEraserMode(uint8_t mode);
    void setShapeType(int32_t type);

    void duplicateSelected();
    void copySelected();
    void paste();
    void pasteAt(float x, float y);
    bool hasClipboard() const;
    void lockSelected();
    void deleteSelected();
    void flipHorizontalSelected();
    void flipVerticalSelected();
    void bringToFrontSelected();
    void sendBackwardSelected();
    void sendToBackSelected();
    void bringForwardSelected();
    void setSelectedColor(Color c);
    Color getSelectedColor() const;
    void toggleFillSelected();
    bool hasSelectedShape() const;
    bool hasSelectedFilledShape() const;

    // ── Document API ──────────────────────────────────────────────────────
    void newDocument();
    bool saveDocument(const std::string& path);
    bool openDocument(const std::string& path);
    bool exportPdf(const std::string& path);
    uint32_t addImage(const std::string& path, uint32_t textureId, float posX, float posY, float width, float height);
    uint32_t uploadTexture(const uint8_t* pixels, int32_t w, int32_t h, bool hasAlpha = true);

    // ── Page API ──────────────────────────────────────────────────────────
    void   addPage();
    void   insertPage(size_t index);
    void   duplicatePage(size_t index);
    void   deletePage(size_t index);
    void   setActivePage(size_t index);
    void   clearActivePage();
    void   setActivePageBackgroundColor(Color c);
    void   setActivePageGridType(int gridType);
    void   setAllPagesBackground(Color c, int gridType);
    void   setPageBackgroundTexture(size_t pageIndex, uint32_t textureId, const std::string& imagePath = "");
    uint32_t getPageBackgroundTexture(size_t pageIndex) const;
    std::string getPageBackgroundPath(size_t pageIndex) const;
    uint32_t addImageToPage(size_t pageIndex, const std::string& path, uint32_t textureId, float posX, float posY, float width, float height);
    void   setPageDimensions(size_t pageIndex, float width, float height);
    void   setAllPagesDimensions(float width, float height);
    void   resetDocumentPages(size_t count, float width, float height);
    void   insertPages(size_t insertIndex, size_t count, float width, float height);
    bool   isDocumentEmpty() const;
    size_t pageCount()       const;
    size_t activePageIndex() const;
    void   reorderPage(size_t fromIdx, size_t toIdx);
    bool   renderPageThumbnail(size_t pageIndex, void* rgbaPixels, int32_t width, int32_t height, bool clearBackground = true);
    std::string getPageExportJson(size_t pageIndex) const;

    // ── History ────────────────────────────────────────────────────────────
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // ── Viewport ──────────────────────────────────────────────────────────
    void fitPageToScreen();
    void zoomTo(float factor, Vec2f screenFocus);

    // ── Recovery ──────────────────────────────────────────────────────────
    bool hasRecoveryFile() const;
    bool recoverSession();
    void discardRecovery();

    // ── Engine Accessors (used internally and by tools) ───────────────────
    Document&        document()    { return m_document; }
    const Document&  document()    const { return m_document; }
    CanvasCamera&    camera()      { return *m_camera; }
    ToolManager&     tools()       { return *m_toolMgr; }
    IRenderPipeline* renderer()    { return m_renderer; }

    void pushCommand(std::unique_ptr<Command> cmd);
    void invalidate(uint32_t flags = DIRTY_ALL);

    // ── Renderer Stats (for debug overlay) ───────────────────────────────
    uint32_t rendererFps()   const;
    float    rendererCpuMs() const;

private:
    EngineConfig    m_config;
    EngineCallbacks m_callbacks;
    Document        m_document;

    // Owned subsystems
    std::unique_ptr<CanvasCamera>    m_camera;
    std::unique_ptr<ToolManager>     m_toolMgr;
    std::unordered_map<uint32_t, std::unique_ptr<CommandStack>> m_pageCmdStacks;
    std::unique_ptr<AutoSaveManager> m_autoSave;
    std::unique_ptr<InputDispatcher> m_inputDispatcher;
    IRenderPipeline*                 m_renderer = nullptr; // owned

    CommandStack* getOrCreatePageCommandStack(uint32_t pageId);
    CommandStack* activePageCommandStack();

    // Active tool state
    ToolType    m_activeToolType = ToolType::PEN;
    StrokeStyle m_activeStyle;

    // Thread safety
    mutable std::mutex       m_documentMutex;
    std::atomic<uint32_t>    m_dirtyFlags{DIRTY_ALL};
    bool                     m_initialized = false;
};

} // namespace ob
