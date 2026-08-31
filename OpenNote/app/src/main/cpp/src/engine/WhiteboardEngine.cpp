#include "ob/WhiteboardEngine.h"
#include "ob/IRenderPipeline.h"
#include "ob/CanvasCamera.h"
#include "ob/Tools.h"
#include "ob/CommandStack.h"
#include "ob/AutoSaveManager.h"
#include "ob/InputSystem.h"
#include "ob/FileFormat.h"
#include <android/log.h>
#include <cstdlib>
#include <sstream>
#include <iomanip>

#define LOG_TAG "OB_Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ob {

// ─── Construction ─────────────────────────────────────────────────────────────

WhiteboardEngine::WhiteboardEngine(const EngineConfig& config)
    : m_config(config)
{
    m_camera       = std::make_unique<CanvasCamera>();
    m_toolMgr      = std::make_unique<ToolManager>(this);
    m_autoSave     = std::make_unique<AutoSaveManager>(this,
                                                       config.autoSavePath,
                                                       config.autoSaveIntervalSec);

    m_activeStyle.color   = Color::black();
    m_activeStyle.width   = 4.0f;
    m_activeStyle.opacity = 1.0f;
    m_toolMgr->penTool()->setStyle(m_activeStyle);

    LOGI("WhiteboardEngine created. auto-save=%s", config.autoSavePath.c_str());
}

CommandStack* WhiteboardEngine::getOrCreatePageCommandStack(uint32_t pageId) {
    auto it = m_pageCmdStacks.find(pageId);
    if (it == m_pageCmdStacks.end()) {
        auto stack = std::make_unique<CommandStack>(m_config.undoHistoryLimit);
        stack->onChanged = [this, pageId](bool canUndo, bool canRedo) {
            const Page* activePg = m_document.activePage_ptr();
            if (activePg && activePg->id == pageId) {
                if (m_callbacks.onUndoRedoChanged) {
                    m_callbacks.onUndoRedoChanged(canUndo, canRedo);
                }
            }
            m_autoSave->markDirty();
            if (m_callbacks.onDocumentDirtyChanged) m_callbacks.onDocumentDirtyChanged(true);
        };
        auto* ptr = stack.get();
        m_pageCmdStacks[pageId] = std::move(stack);
        return ptr;
    }
    return it->second.get();
}

CommandStack* WhiteboardEngine::activePageCommandStack() {
    const Page* activePg = m_document.activePage_ptr();
    if (!activePg) return nullptr;
    return getOrCreatePageCommandStack(activePg->id);
}

WhiteboardEngine::~WhiteboardEngine() {
    destroyRenderer();
    m_autoSave->stop();
}

// ─── Platform Wiring ──────────────────────────────────────────────────────────

void WhiteboardEngine::setCallbacks(EngineCallbacks callbacks) {
    m_callbacks = std::move(callbacks);
}

bool WhiteboardEngine::initRenderer(IRenderPipeline* renderer, void* nativeWindow,
                                    int32_t w, int32_t h) {
    if (!renderer) {
        LOGE("initRenderer: null renderer");
        return false;
    }

    if (m_renderer) {
        m_renderer->destroy();
        delete m_renderer;
    }
    m_renderer = renderer;

    if (!m_renderer->init(nativeWindow, w, h)) {
        LOGE("Renderer init failed");
        if (m_callbacks.onError) m_callbacks.onError("Renderer initialization failed");
        return false;
    }

    m_camera->setViewport(w, h);
    m_camera->fitToPage(m_document.pages[0]);

    // Start input dispatcher now that camera is ready
    m_inputDispatcher = std::make_unique<InputDispatcher>(
        m_toolMgr.get(), m_camera.get(),
        m_config.palmRejection, m_config.touchRadiusThreshold,
        m_config.enableKalmanFilter);

    // Configure auto-save completion callback and start thread
    m_autoSave->onAutoSaveComplete = [this](bool success) {
        if (m_callbacks.onAutoSaveComplete) {
            m_callbacks.onAutoSaveComplete(success);
        }
    };
    m_autoSave->start();

    m_initialized = true;
    m_dirtyFlags.store(DIRTY_ALL);
    LOGI("Renderer initialized: %dx%d", w, h);
    return true;
}

void WhiteboardEngine::onSurfaceChanged(int32_t w, int32_t h) {
    if (m_renderer) m_renderer->onSurfaceChanged(w, h);
    m_camera->setViewport(w, h);
    m_dirtyFlags.store(DIRTY_ALL);
}

void WhiteboardEngine::destroyRenderer() {
    if (m_autoSave) {
        m_autoSave->stop();
    }
    if (m_renderer) {
        m_renderer->destroy();
        delete m_renderer;
        m_renderer = nullptr;
    }
    m_initialized = false;
}

// ─── Render ───────────────────────────────────────────────────────────────────

void WhiteboardEngine::renderFrame() {
    if (!m_renderer || !m_initialized) return;
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_renderer->setLassoPath(&m_toolMgr->selectionTool()->lassoPath());
        if (m_toolMgr->selectionTool()->hasSelection()) {
            m_renderer->setSelectionBounds(&m_toolMgr->selectionTool()->selectionBounds(),
                                           m_toolMgr->selectionTool()->rotation());
        } else {
            m_renderer->setSelectionBounds(nullptr, 0.0f);
        }
    } else {
        m_renderer->setLassoPath(nullptr);
        m_renderer->setSelectionBounds(nullptr, 0.0f);
    }
    if (m_toolMgr && m_toolMgr->shapeTool()) {
        m_renderer->setLiveShape(m_toolMgr->shapeTool()->liveShape());
    } else {
        m_renderer->setLiveShape(nullptr);
    }
    m_renderer->renderFrame(m_document, *m_camera);
}

// ─── Input ────────────────────────────────────────────────────────────────────

void WhiteboardEngine::onTouchEvent(const TouchEvent& event) {
    if (!m_initialized || !m_inputDispatcher) return;
    m_inputDispatcher->dispatch(event);
    invalidate(DIRTY_STROKES);
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
}

// ─── Tool API ─────────────────────────────────────────────────────────────────

void WhiteboardEngine::setActiveTool(ToolType tool) {
    m_activeToolType = tool;
    int8_t toolIndex = static_cast<int8_t>(tool);
    if (toolIndex == -1) {
        m_toolMgr->setActiveTool(ActiveTool::SELECTION);
    } else if (toolIndex == -2) {
        m_toolMgr->setActiveTool(ActiveTool::HAND);
    } else if (toolIndex == -3) {
        m_toolMgr->setActiveTool(ActiveTool::SHAPE);
    } else if (toolIndex == 3 || toolIndex == 4) {
        m_toolMgr->setActiveTool(ActiveTool::ERASER);
    } else {
        m_toolMgr->setActiveTool(ActiveTool::PEN);
    }
}

void WhiteboardEngine::setStrokeColor(Color c) {
    m_activeStyle.color = c;
    m_toolMgr->penTool()->setStyle(m_activeStyle);
    m_toolMgr->shapeTool()->setStrokeColor(c);
}

void WhiteboardEngine::setStrokeWidth(float w) {
    m_activeStyle.width = w;
    m_toolMgr->penTool()->setStyle(m_activeStyle);
    m_toolMgr->shapeTool()->setStrokeWidth(w);
}

void WhiteboardEngine::setStrokeOpacity(float op) {
    m_activeStyle.opacity = op;
    m_toolMgr->penTool()->setStyle(m_activeStyle);
}

void WhiteboardEngine::setPenType(uint8_t penType) {
    m_activeStyle.penType = penType;
    m_toolMgr->penTool()->setPenType(penType);
    m_toolMgr->penTool()->setStyle(m_activeStyle);
}

void WhiteboardEngine::setEraserSize(float sz) {
    m_toolMgr->eraserTool()->setRadius(sz * 0.5f);
}

void WhiteboardEngine::setEraserMode(uint8_t mode) {
    m_toolMgr->eraserTool()->setMode(static_cast<EraserMode>(mode));
}

void WhiteboardEngine::setShapeType(int32_t type) {
    m_toolMgr->shapeTool()->setShapeType(static_cast<ShapeType>(type));
}

void WhiteboardEngine::duplicateSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->duplicateSelected();
        invalidate(DIRTY_STROKES);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

void WhiteboardEngine::copySelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->copySelected();
    }
}

void WhiteboardEngine::paste() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->paste();
        invalidate(DIRTY_STROKES | DIRTY_SHAPES);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

void WhiteboardEngine::pasteAt(float x, float y) {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->pasteAt({x, y});
        invalidate(DIRTY_STROKES | DIRTY_SHAPES);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

bool WhiteboardEngine::hasClipboard() const {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        return m_toolMgr->selectionTool()->hasClipboard();
    }
    return false;
}

void WhiteboardEngine::lockSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->lockSelected();
        invalidate(DIRTY_STROKES);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

void WhiteboardEngine::deleteSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->deleteSelected();
        invalidate(DIRTY_STROKES);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

void WhiteboardEngine::flipHorizontalSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->flipHorizontalSelected();
    }
}

void WhiteboardEngine::flipVerticalSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->flipVerticalSelected();
    }
}

void WhiteboardEngine::bringToFrontSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->bringToFrontSelected();
    }
}

void WhiteboardEngine::sendBackwardSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->sendBackwardSelected();
    }
}

void WhiteboardEngine::sendToBackSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->sendToBackSelected();
    }
}

void WhiteboardEngine::bringForwardSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->bringForwardSelected();
    }
}

void WhiteboardEngine::setSelectedColor(Color c) {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->setSelectedColor(c);
        invalidate(DIRTY_STROKES);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

Color WhiteboardEngine::getSelectedColor() const {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        return m_toolMgr->selectionTool()->getSelectedColor();
    }
    return Color{0, 0, 0, 255};
}

void WhiteboardEngine::toggleFillSelected() {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        m_toolMgr->selectionTool()->toggleFillSelected();
    }
}

bool WhiteboardEngine::hasSelectedShape() const {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        return m_toolMgr->selectionTool()->hasSelectedShape();
    }
    return false;
}

bool WhiteboardEngine::hasSelectedFilledShape() const {
    if (m_toolMgr && m_toolMgr->selectionTool()) {
        return m_toolMgr->selectionTool()->hasSelectedFilledShape();
    }
    return false;
}

// ─── Document API ─────────────────────────────────────────────────────────────

void WhiteboardEngine::newDocument() {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    m_document = Document{};
    m_pageCmdStacks.clear();
    m_toolMgr->selectionTool()->clearSelection();
    m_camera->fitToPage(m_document.pages[0]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) m_callbacks.onUndoRedoChanged(false, false);
    LOGI("New document created");
}

bool WhiteboardEngine::saveDocument(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    auto res = obnSave(m_document, path);
    if (res.success) {
        LOGI("Document saved to %s (%zu bytes)", path.c_str(), res.bytesWritten);
        m_autoSave->writeCleanExit();
        if (m_callbacks.onDocumentDirtyChanged) m_callbacks.onDocumentDirtyChanged(false);
    } else {
        LOGE("Save failed: %s", res.errorMessage.c_str());
        if (m_callbacks.onError) m_callbacks.onError(res.errorMessage);
    }
    return res.success;
}

bool WhiteboardEngine::openDocument(const std::string& path) {
    Document newDoc;
    auto res = obnLoad(newDoc, path);
    if (!res.success) {
        LOGE("Load failed: %s", res.errorMessage.c_str());
        if (m_callbacks.onError) m_callbacks.onError(res.errorMessage);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_documentMutex);
        m_document = std::move(newDoc);
        m_pageCmdStacks.clear();
        m_toolMgr->selectionTool()->clearSelection();
        m_camera->fitToPage(m_document.pages[0]);
        m_dirtyFlags.store(DIRTY_ALL);
    }
    LOGI("Document loaded: %zu pages, %zu strokes", res.pagesLoaded, res.strokesLoaded);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    return true;
}

bool WhiteboardEngine::exportPdf(const std::string& /*path*/) {
    // TODO Phase 3: PDF export via PDFium / platform print
    LOGI("exportPdf: not yet implemented");
    return false;
}

uint32_t WhiteboardEngine::uploadTexture(const uint8_t* pixels, int32_t w, int32_t h, bool hasAlpha) {
    if (!m_renderer) return 0;
    return m_renderer->uploadTexture(pixels, w, h, hasAlpha);
}

uint32_t WhiteboardEngine::addImage(const std::string& path, uint32_t textureId, float posX, float posY, float width, float height) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    Page* p = m_document.activePage_ptr();
    if (!p) return 0;

    ImageElement img;
    img.id = (uint32_t)(p->images.size() + 1);
    img.textureId = textureId;
    img.imagePath = path;
    img.bounds = Rectf{posX, posY, posX + width, posY + height};
    img.opacity = 1.0f;
    img.isErased = false;
    img.isSelected = false;
    img.isLocked = false;

    p->images.push_back(img);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    LOGI("Added image ID=%u at [%.1f, %.1f, %.1f, %.1f], tex=%u",
         img.id, img.bounds.left, img.bounds.top, img.bounds.right, img.bounds.bottom, textureId);
    return img.id;
}

// ─── Page API ─────────────────────────────────────────────────────────────────

void WhiteboardEngine::addPage() {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    m_document.appendNewPage();
    m_document.activePage = (uint32_t)(m_document.pages.size() - 1);
    m_camera->fitToPage(m_document.pages[m_document.activePage]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) {
        m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    }
}

void WhiteboardEngine::deletePage(size_t index) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (index >= m_document.pages.size()) return;
    if (m_document.pages.size() <= 1) {
        // Deleting the only page wipes its content and keeps 1 fresh clean page
        m_document.pages[0].strokes.clear();
        m_document.pages[0].shapes.clear();
        m_document.pages[0].images.clear();
        auto* stack = activePageCommandStack();
        if (stack) stack->clear();
        m_toolMgr->selectionTool()->clearSelection();
        m_camera->fitToPage(m_document.pages[0]);
        m_dirtyFlags.store(DIRTY_ALL);
        if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
        if (m_callbacks.onUndoRedoChanged) m_callbacks.onUndoRedoChanged(false, false);
        return;
    }
    uint32_t delId = m_document.pages[index].id;
    m_pageCmdStacks.erase(delId);
    m_document.pages.erase(m_document.pages.begin() + (ptrdiff_t)index);
    if (m_document.activePage >= (uint32_t)m_document.pages.size())
        m_document.activePage = (uint32_t)m_document.pages.size() - 1;
    m_camera->fitToPage(m_document.pages[m_document.activePage]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) {
        m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    }
}

void WhiteboardEngine::setActivePage(size_t index) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (index >= m_document.pages.size()) return;
    m_document.activePage = (uint32_t)index;
    m_toolMgr->selectionTool()->clearSelection();
    m_camera->fitToPage(m_document.pages[index]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) {
        m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    }
}

void WhiteboardEngine::clearActivePage() {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    Page* page = m_document.activePage_ptr();
    if (page) {
        if (!page->strokes.empty() || !page->shapes.empty() || !page->images.empty()) {
            auto oldStrokes = page->strokes;
            auto oldShapes  = page->shapes;
            auto oldImages  = page->images;

            page->strokes.clear();
            page->shapes.clear();
            page->images.clear();

            if (m_toolMgr && m_toolMgr->selectionTool()) {
                m_toolMgr->selectionTool()->clearSelection();
            }

            pushCommand(std::make_unique<ClearPageCommand>(
                this, page->id, std::move(oldStrokes), std::move(oldShapes), std::move(oldImages)
            ));

            m_dirtyFlags.store(DIRTY_ALL);
            if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
        }
    }
}

void WhiteboardEngine::setActivePageBackgroundColor(Color c) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    Page* page = m_document.activePage_ptr();
    if (page) {
        page->background.color = c;
        m_dirtyFlags.store(DIRTY_ALL);
    }
}

void WhiteboardEngine::setActivePageGridType(int gridType) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    Page* page = m_document.activePage_ptr();
    if (page) {
        page->background.gridType = gridType;
        m_dirtyFlags.store(DIRTY_ALL);
    }
}

void WhiteboardEngine::setAllPagesBackground(Color c, int gridType) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    for (auto& pg : m_document.pages) {
        pg.background.color = c;
        pg.background.gridType = gridType;
    }
    m_dirtyFlags.store(DIRTY_ALL);
}

void WhiteboardEngine::setPageBackgroundTexture(size_t pageIndex, uint32_t textureId, const std::string& imagePath) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (pageIndex < m_document.pages.size()) {
        if (textureId > 0 || imagePath.empty()) {
            m_document.pages[pageIndex].background.textureId = textureId;
        }
        if (!imagePath.empty()) {
            m_document.pages[pageIndex].background.imagePath = imagePath;
        }
        m_dirtyFlags.store(DIRTY_ALL);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

uint32_t WhiteboardEngine::getPageBackgroundTexture(size_t pageIndex) const {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (pageIndex < m_document.pages.size()) {
        return m_document.pages[pageIndex].background.textureId;
    }
    return 0;
}

std::string WhiteboardEngine::getPageBackgroundPath(size_t pageIndex) const {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (pageIndex < m_document.pages.size()) {
        return m_document.pages[pageIndex].background.imagePath;
    }
    return "";
}

uint32_t WhiteboardEngine::addImageToPage(size_t pageIndex, const std::string& path, uint32_t textureId, float posX, float posY, float width, float height) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (pageIndex >= m_document.pages.size()) return 0;
    Page& p = m_document.pages[pageIndex];

    ImageElement img;
    img.id = (uint32_t)(p.images.size() + 1);
    img.textureId = textureId;
    img.imagePath = path;
    img.bounds = Rectf{posX, posY, posX + width, posY + height};
    img.opacity = 1.0f;
    img.isErased = false;
    img.isSelected = false;
    img.isLocked = false;

    p.images.push_back(img);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    return img.id;
}

void WhiteboardEngine::setPageDimensions(size_t pageIndex, float width, float height) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (pageIndex < m_document.pages.size() && width > 0.0f && height > 0.0f) {
        m_document.pages[pageIndex].width = width;
        m_document.pages[pageIndex].height = height;
        if (pageIndex == (size_t)m_document.activePage) {
            m_camera->fitToPage(m_document.pages[pageIndex]);
        }
        m_dirtyFlags.store(DIRTY_ALL);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

void WhiteboardEngine::setAllPagesDimensions(float width, float height) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (width > 0.0f && height > 0.0f && !m_document.pages.empty()) {
        for (auto& pg : m_document.pages) {
            pg.width = width;
            pg.height = height;
        }
        if (m_document.activePage < m_document.pages.size()) {
            m_camera->fitToPage(m_document.pages[m_document.activePage]);
        }
        m_dirtyFlags.store(DIRTY_ALL);
        if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
    }
}

void WhiteboardEngine::resetDocumentPages(size_t count, float width, float height) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (m_renderer) {
        for (const auto& pg : m_document.pages) {
            if (pg.background.textureId > 0) {
                m_renderer->deleteTexture(pg.background.textureId);
            }
            for (const auto& img : pg.images) {
                if (img.textureId > 0) {
                    m_renderer->deleteTexture(img.textureId);
                }
            }
        }
    }
    m_document.pages.clear();
    m_pageCmdStacks.clear();
    m_toolMgr->selectionTool()->clearSelection();

    size_t n = count > 0 ? count : 1;
    for (size_t i = 0; i < n; i++) {
        Page p;
        p.id = (uint32_t)(i + 1);
        p.width = width > 0.0f ? width : 1920.0f;
        p.height = height > 0.0f ? height : 1080.0f;
        m_document.pages.push_back(p);
    }
    m_document.activePage = 0;
    m_camera->fitToPage(m_document.pages[0]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) m_callbacks.onUndoRedoChanged(false, false);
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
}

void WhiteboardEngine::insertPages(size_t insertIndex, size_t count, float width, float height) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (count == 0) return;

    uint32_t maxId = 0;
    for (const auto& pg : m_document.pages) {
        if (pg.id > maxId) maxId = pg.id;
    }

    size_t insIdx = insertIndex <= m_document.pages.size() ? insertIndex : m_document.pages.size();
    float w = width > 0.0f ? width : 1920.0f;
    float h = height > 0.0f ? height : 1080.0f;

    std::vector<Page> newPages;
    newPages.reserve(count);
    for (size_t i = 0; i < count; i++) {
        Page p;
        p.id = maxId + 1 + (uint32_t)i;
        p.width = w;
        p.height = h;
        newPages.push_back(std::move(p));
    }

    m_document.pages.insert(m_document.pages.begin() + (ptrdiff_t)insIdx, newPages.begin(), newPages.end());
    m_document.activePage = (uint32_t)insIdx;
    m_camera->fitToPage(m_document.pages[m_document.activePage]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) {
        m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    }
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
}

bool WhiteboardEngine::isDocumentEmpty() const {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (m_document.pages.size() > 1) return false;
    if (m_document.pages.empty()) return true;
    const auto& p0 = m_document.pages[0];
    return p0.strokes.empty() && p0.shapes.empty() && p0.images.empty() && p0.background.imagePath.empty();
}

size_t WhiteboardEngine::pageCount() const {
    return m_document.pages.size();
}

size_t WhiteboardEngine::activePageIndex() const {
    return (size_t)m_document.activePage;
}

void WhiteboardEngine::insertPage(size_t index) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    Page p;
    uint32_t maxId = 0;
    for (const auto& pg : m_document.pages) {
        if (pg.id > maxId) maxId = pg.id;
    }
    p.id = maxId + 1;
    size_t insIdx = index <= m_document.pages.size() ? index : m_document.pages.size();
    m_document.pages.insert(m_document.pages.begin() + (ptrdiff_t)insIdx, std::move(p));
    m_document.activePage = (uint32_t)insIdx;
    m_camera->fitToPage(m_document.pages[m_document.activePage]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) {
        m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    }
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
}

void WhiteboardEngine::duplicatePage(size_t index) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (index >= m_document.pages.size()) return;
    Page newPg = m_document.pages[index]; // value copy of strokes, shapes, background
    uint32_t maxId = 0;
    for (const auto& pg : m_document.pages) {
        if (pg.id > maxId) maxId = pg.id;
    }
    newPg.id = maxId + 1;
    size_t insIdx = index + 1;
    m_document.pages.insert(m_document.pages.begin() + (ptrdiff_t)insIdx, std::move(newPg));
    m_document.activePage = (uint32_t)insIdx;
    m_camera->fitToPage(m_document.pages[m_document.activePage]);
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) {
        m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    }
    if (m_callbacks.onInvalidate) m_callbacks.onInvalidate();
}

void WhiteboardEngine::reorderPage(size_t fromIdx, size_t toIdx) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (fromIdx == toIdx) return;
    if (fromIdx >= m_document.pages.size() || toIdx >= m_document.pages.size()) return;
    Page pg = std::move(m_document.pages[fromIdx]);
    m_document.pages.erase(m_document.pages.begin() + (ptrdiff_t)fromIdx);
    m_document.pages.insert(m_document.pages.begin() + (ptrdiff_t)toIdx, std::move(pg));
    m_document.activePage = (uint32_t)toIdx;
    m_dirtyFlags.store(DIRTY_ALL);
    if (m_callbacks.onPagesChanged) m_callbacks.onPagesChanged();
    if (m_callbacks.onUndoRedoChanged) {
        m_callbacks.onUndoRedoChanged(canUndo(), canRedo());
    }
}

static inline void blendPixel(uint32_t* dst, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || x >= w || y < 0 || y >= h || a == 0) return;
    int32_t idx = y * w + x;
    if (a >= 250) {
        dst[idx] = (0xFF << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        return;
    }
    uint32_t bg = dst[idx];
    uint32_t bgR = bg & 0xFF;
    uint32_t bgG = (bg >> 8) & 0xFF;
    uint32_t bgB = (bg >> 16) & 0xFF;
    uint32_t invA = 255 - a;
    uint32_t outR = (r * a + bgR * invA) / 255;
    uint32_t outG = (g * a + bgG * invA) / 255;
    uint32_t outB = (b * a + bgB * invA) / 255;
    dst[idx] = (0xFF << 24) | (outB << 16) | (outG << 8) | outR;
}

bool WhiteboardEngine::renderPageThumbnail(size_t pageIndex, void* rgbaPixels, int32_t width, int32_t height, bool clearBackground) {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (!rgbaPixels || width <= 0 || height <= 0 || pageIndex >= m_document.pages.size()) return false;
    const Page& page = m_document.pages[pageIndex];

    uint32_t* dst = static_cast<uint32_t*>(rgbaPixels);
    if (clearBackground) {
        uint32_t bgCol = (0xFF << 24) |
                         ((uint32_t)page.background.color.b << 16) |
                         ((uint32_t)page.background.color.g << 8)  |
                         ((uint32_t)page.background.color.r);
        for (int32_t i = 0; i < width * height; i++) {
            dst[i] = bgCol;
        }
    }

    float scaleX = (float)width  / (page.width > 0.0f ? page.width : 1920.0f);
    float scaleY = (float)height / (page.height > 0.0f ? page.height : 1080.0f);
    float avgScale = (scaleX + scaleY) * 0.5f;

    // 1. Shapes
    for (const auto& sh : page.shapes) {
        if (sh.isErased) continue;
        int32_t x0 = (int32_t)(std::min(sh.bounds.left, sh.bounds.right) * scaleX);
        int32_t y0 = (int32_t)(std::min(sh.bounds.top, sh.bounds.bottom) * scaleY);
        int32_t x1 = (int32_t)(std::max(sh.bounds.left, sh.bounds.right) * scaleX);
        int32_t y1 = (int32_t)(std::max(sh.bounds.top, sh.bounds.bottom) * scaleY);
        x0 = std::max(0, std::min(width - 1, x0));
        y0 = std::max(0, std::min(height - 1, y0));
        x1 = std::max(0, std::min(width - 1, x1));
        y1 = std::max(0, std::min(height - 1, y1));

        uint8_t shA = sh.strokeColor.a;
        int32_t thick = std::max(1, (int32_t)(sh.strokeWidth * avgScale));

        for (int32_t t = 0; t < thick; t++) {
            for (int32_t x = x0; x <= x1; x++) {
                blendPixel(dst, x, y0 + t, width, height, sh.strokeColor.r, sh.strokeColor.g, sh.strokeColor.b, shA);
                blendPixel(dst, x, y1 - t, width, height, sh.strokeColor.r, sh.strokeColor.g, sh.strokeColor.b, shA);
            }
            for (int32_t y = y0; y <= y1; y++) {
                blendPixel(dst, x0 + t, y, width, height, sh.strokeColor.r, sh.strokeColor.g, sh.strokeColor.b, shA);
                blendPixel(dst, x1 - t, y, width, height, sh.strokeColor.r, sh.strokeColor.g, sh.strokeColor.b, shA);
            }
        }
    }

    // 2. Strokes
    for (const auto& [id, s] : page.strokes) {
        if (s.isErased || s.points.empty()) continue;
        uint8_t sA = (uint8_t)(s.style.color.a * s.style.opacity);
        int32_t rad = std::max(1, (int32_t)(s.style.width * avgScale * 0.5f));

        for (size_t i = 1; i < s.points.size(); i++) {
            int32_t px0 = (int32_t)(s.points[i-1].x * scaleX);
            int32_t py0 = (int32_t)(s.points[i-1].y * scaleY);
            int32_t px1 = (int32_t)(s.points[i].x * scaleX);
            int32_t py1 = (int32_t)(s.points[i].y * scaleY);

            int32_t ldx = std::abs(px1 - px0), sx = px0 < px1 ? 1 : -1;
            int32_t ldy = -std::abs(py1 - py0), sy = py0 < py1 ? 1 : -1;
            int32_t err = ldx + ldy, e2;
            int32_t cx = px0, cy = py0;
            while (true) {
                if (rad <= 1) {
                    blendPixel(dst, cx, cy, width, height, s.style.color.r, s.style.color.g, s.style.color.b, sA);
                } else {
                    for (int32_t dy = -rad; dy <= rad; dy++) {
                        for (int32_t dx = -rad; dx <= rad; dx++) {
                            if (dx * dx + dy * dy <= rad * rad) {
                                blendPixel(dst, cx + dx, cy + dy, width, height, s.style.color.r, s.style.color.g, s.style.color.b, sA);
                            }
                        }
                    }
                }
                if (cx == px1 && cy == py1) break;
                e2 = 2 * err;
                if (e2 >= ldy) { err += ldy; cx += sx; }
                if (e2 <= ldx) { err += ldx; cy += sy; }
            }
        }
    }

    return true;
}

std::string WhiteboardEngine::getPageExportJson(size_t pageIndex) const {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    if (pageIndex >= m_document.pages.size()) {
        return "{}";
    }
    const Page& page = m_document.pages[pageIndex];

    std::ostringstream ss;
    ss << "{";
    ss << "\"pageIndex\":" << pageIndex << ",";
    ss << "\"width\":" << (page.width > 0.0f ? page.width : 1920.0f) << ",";
    ss << "\"height\":" << (page.height > 0.0f ? page.height : 1080.0f) << ",";

    // Background
    Color bgCol = page.background.color;
    if (bgCol.a == 0 && bgCol.r == 0 && bgCol.g == 0 && bgCol.b == 0) {
        bgCol = Color::white();
    }
    uint32_t bgARGB = ((uint32_t)bgCol.a << 24) |
                      ((uint32_t)bgCol.r << 16) |
                      ((uint32_t)bgCol.g << 8)  |
                      ((uint32_t)bgCol.b);
    ss << "\"background\":{";
    ss << "\"color\":" << bgARGB << ",";
    ss << "\"gridType\":" << page.background.gridType << ",";
    ss << "\"imagePath\":\"";
    for (char c : page.background.imagePath) {
        if (c == '\\') ss << "\\\\";
        else if (c == '"') ss << "\\\"";
        else ss << c;
    }
    ss << "\"},";

    // Strokes
    ss << "\"strokes\":[";
    bool firstStroke = true;
    for (const auto& [id, s] : page.strokes) {
        if (s.isErased || s.points.empty()) continue;
        if (!firstStroke) ss << ",";
        firstStroke = false;

        uint32_t colorARGB = ((uint32_t)s.style.color.a << 24) |
                             ((uint32_t)s.style.color.r << 16) |
                             ((uint32_t)s.style.color.g << 8)  |
                             ((uint32_t)s.style.color.b);
        ss << "{";
        ss << "\"id\":" << s.id << ",";
        ss << "\"color\":" << colorARGB << ",";
        ss << "\"width\":" << s.style.width << ",";
        ss << "\"opacity\":" << s.style.opacity << ",";
        ss << "\"penType\":" << (int)s.style.penType << ",";
        ss << "\"points\":[";
        for (size_t i = 0; i < s.points.size(); i++) {
            if (i > 0) ss << ",";
            ss << "{\"x\":" << s.points[i].x << ",\"y\":" << s.points[i].y << ",\"p\":" << s.points[i].pressure << "}";
        }
        ss << "]}";
    }
    ss << "],";

    // Shapes
    ss << "\"shapes\":[";
    bool firstShape = true;
    for (const auto& sh : page.shapes) {
        if (sh.isErased) continue;
        if (!firstShape) ss << ",";
        firstShape = false;

        uint32_t sColorARGB = ((uint32_t)sh.strokeColor.a << 24) |
                              ((uint32_t)sh.strokeColor.r << 16) |
                              ((uint32_t)sh.strokeColor.g << 8)  |
                              ((uint32_t)sh.strokeColor.b);
        uint32_t fColorARGB = ((uint32_t)sh.fillColor.a << 24) |
                              ((uint32_t)sh.fillColor.r << 16) |
                              ((uint32_t)sh.fillColor.g << 8)  |
                              ((uint32_t)sh.fillColor.b);

        ss << "{";
        ss << "\"id\":" << sh.id << ",";
        ss << "\"type\":" << (int)sh.shapeType << ",";
        ss << "\"left\":" << sh.bounds.left << ",";
        ss << "\"top\":" << sh.bounds.top << ",";
        ss << "\"right\":" << sh.bounds.right << ",";
        ss << "\"bottom\":" << sh.bounds.bottom << ",";
        ss << "\"strokeColor\":" << sColorARGB << ",";
        ss << "\"fillColor\":" << fColorARGB << ",";
        ss << "\"strokeWidth\":" << sh.strokeWidth << ",";
        ss << "\"rotation\":" << sh.rotation;
        ss << "}";
    }
    ss << "],";

    // Images
    ss << "\"images\":[";
    bool firstImg = true;
    for (const auto& img : page.images) {
        if (img.isErased) continue;
        if (!firstImg) ss << ",";
        firstImg = false;

        ss << "{";
        ss << "\"id\":" << img.id << ",";
        ss << "\"left\":" << img.bounds.left << ",";
        ss << "\"top\":" << img.bounds.top << ",";
        ss << "\"right\":" << img.bounds.right << ",";
        ss << "\"bottom\":" << img.bounds.bottom << ",";
        ss << "\"opacity\":" << img.opacity << ",";
        ss << "\"rotation\":" << img.rotation << ",";
        ss << "\"path\":\"";
        for (char c : img.imagePath) {
            if (c == '\\') ss << "\\\\";
            else if (c == '"') ss << "\\\"";
            else ss << c;
        }
        ss << "\"}";
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

// ─── Undo/Redo ────────────────────────────────────────────────────────────────

void WhiteboardEngine::undo() {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    auto* stack = activePageCommandStack();
    if (stack) {
        stack->undo();
        m_toolMgr->selectionTool()->clearSelection();
        m_dirtyFlags.store(DIRTY_ALL);
    }
}

void WhiteboardEngine::redo() {
    std::lock_guard<std::mutex> lock(m_documentMutex);
    auto* stack = activePageCommandStack();
    if (stack) {
        stack->redo();
        m_toolMgr->selectionTool()->clearSelection();
        m_dirtyFlags.store(DIRTY_ALL);
    }
}

bool WhiteboardEngine::canUndo() const {
    const Page* activePg = m_document.activePage_ptr();
    if (!activePg) return false;
    auto it = m_pageCmdStacks.find(activePg->id);
    return it != m_pageCmdStacks.end() && it->second->canUndo();
}

bool WhiteboardEngine::canRedo() const {
    const Page* activePg = m_document.activePage_ptr();
    if (!activePg) return false;
    auto it = m_pageCmdStacks.find(activePg->id);
    return it != m_pageCmdStacks.end() && it->second->canRedo();
}

void WhiteboardEngine::pushCommand(std::unique_ptr<Command> cmd) {
    if (!cmd) return;
    uint32_t pid = cmd->pageId();
    if (pid == 0) {
        const Page* activePg = m_document.activePage_ptr();
        if (activePg) pid = activePg->id;
    }
    auto* stack = getOrCreatePageCommandStack(pid);
    if (stack) {
        stack->push(std::move(cmd));
    }
}

// ─── Viewport ─────────────────────────────────────────────────────────────────

void WhiteboardEngine::fitPageToScreen() {
    const Page* pg = m_document.activePage_ptr();
    if (pg) m_camera->fitToPage(*pg);
    m_dirtyFlags.store(DIRTY_CAMERA);
}

void WhiteboardEngine::zoomTo(float factor, Vec2f screenFocus) {
    m_camera->zoomToLevel(factor, screenFocus);
    m_dirtyFlags.store(DIRTY_CAMERA);
}

// ─── Recovery ─────────────────────────────────────────────────────────────────

bool WhiteboardEngine::hasRecoveryFile() const {
    return m_autoSave->hasRecovery();
}

bool WhiteboardEngine::recoverSession() {
    std::string recPath = m_config.autoSavePath + "/.recovery.obn";
    return openDocument(recPath);
}

void WhiteboardEngine::discardRecovery() {
    m_autoSave->writeCleanExit();
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

void WhiteboardEngine::invalidate(uint32_t flags) {
    m_dirtyFlags.fetch_or(flags);
}

uint32_t WhiteboardEngine::rendererFps()   const {
    return m_renderer ? m_renderer->lastFps()   : 0;
}

float WhiteboardEngine::rendererCpuMs()    const {
    return m_renderer ? m_renderer->lastCpuMs() : 0.0f;
}

} // namespace ob
