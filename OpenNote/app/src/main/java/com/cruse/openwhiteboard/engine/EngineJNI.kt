package com.cruse.openwhiteboard.engine

import android.util.Log

object EngineJNI {

    private const val TAG = "EngineJNI"
    var isNativeLoaded = false
        private set

    init {
        try {
            System.loadLibrary("openwhiteboard")
            isNativeLoaded = true
            Log.i(TAG, "Native library 'openwhiteboard' loaded successfully")
        } catch (e: Throwable) {
            Log.e(TAG, "Failed to load native library 'openwhiteboard'", e)
            isNativeLoaded = false
        }
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeCreate(filesDir: String): Long
    @JvmStatic external fun nativeSetCallbacks(handle: Long, callbackObj: EngineCallbacksInterface)
    @JvmStatic external fun nativeDestroy(handle: Long)

    // ── Surface ───────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeSurfaceCreated(handle: Long, surface: Any, w: Int, h: Int): Boolean
    @JvmStatic external fun nativeSurfaceChanged(handle: Long, w: Int, h: Int)
    @JvmStatic external fun nativeSurfaceDestroyed(handle: Long)

    // ── Rendering ─────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeRenderFrame(handle: Long)

    // ── Touch ─────────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeTouchEvent(
        handle: Long,
        actionIndex: Int,
        isPen: Boolean,
        ids: IntArray,
        xs: FloatArray,
        ys: FloatArray,
        pressures: FloatArray,
        majors: FloatArray,
        phases: IntArray,
        timestamp: Long
    )

    // ── Tool API ──────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeSetActiveTool(handle: Long, toolIndex: Int)
    @JvmStatic external fun nativeSetStrokeColor(handle: Long, argb: Int)
    @JvmStatic external fun nativeSetStrokeWidth(handle: Long, width: Float)
    @JvmStatic external fun nativeSetStrokeOpacity(handle: Long, opacity: Float)
    @JvmStatic external fun nativeSetPenType(handle: Long, penType: Int)
    @JvmStatic external fun nativeSetEraserSize(handle: Long, size: Float)
    @JvmStatic external fun nativeSetEraserMode(handle: Long, mode: Int)
    @JvmStatic external fun nativeSetShapeType(handle: Long, shapeType: Int)
    @JvmStatic external fun nativeDuplicateSelected(handle: Long)
    @JvmStatic external fun nativeDeleteSelected(handle: Long)
    @JvmStatic external fun nativeFlipHorizontalSelected(handle: Long)
    @JvmStatic external fun nativeFlipVerticalSelected(handle: Long)
    @JvmStatic external fun nativeBringToFrontSelected(handle: Long)
    @JvmStatic external fun nativeSendBackwardSelected(handle: Long)
    @JvmStatic external fun nativeSendToBackSelected(handle: Long)
    @JvmStatic external fun nativeBringForwardSelected(handle: Long)
    @JvmStatic external fun nativeCopySelected(handle: Long)
    @JvmStatic external fun nativePaste(handle: Long)
    @JvmStatic external fun nativePasteAt(handle: Long, x: Float, y: Float)
    @JvmStatic external fun nativeHasClipboard(handle: Long): Boolean
    @JvmStatic external fun nativeSetSelectedColor(handle: Long, color: Int)
    @JvmStatic external fun nativeGetSelectedColor(handle: Long): Int
    @JvmStatic external fun nativeToggleFillSelected(handle: Long)
    @JvmStatic external fun nativeHasSelectedFilledShape(handle: Long): Boolean
    @JvmStatic external fun nativeHasSelectedShape(handle: Long): Boolean
    @JvmStatic external fun nativeLockSelected(handle: Long)
    @JvmStatic external fun nativeClearSelection(handle: Long)
    @JvmStatic external fun nativeTapSelectAt(handle: Long, screenX: Float, screenY: Float): Boolean
    @JvmStatic external fun nativeCanvasToScreenRect(handle: Long, left: Float, top: Float, right: Float, bottom: Float): FloatArray?
    @JvmStatic external fun nativeScreenToCanvas(handle: Long, sx: Float, sy: Float): FloatArray?

    // ── Document ──────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeNewDocument(handle: Long)
    @JvmStatic external fun nativeSaveDocument(handle: Long, path: String): Boolean
    @JvmStatic external fun nativeOpenDocument(handle: Long, path: String): Boolean
    @JvmStatic external fun nativeExportPdf(handle: Long, path: String): Boolean
    @JvmStatic external fun nativeUploadTexture(handle: Long, byteBuffer: java.nio.ByteBuffer, width: Int, height: Int): Int
    @JvmStatic external fun nativeAddImage(handle: Long, path: String, textureId: Int, posX: Float, posY: Float, width: Float, height: Float): Int

    // ── Pages ─────────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeAddPage(handle: Long)
    @JvmStatic external fun nativeInsertPage(handle: Long, index: Int)
    @JvmStatic external fun nativeDuplicatePage(handle: Long, index: Int)
    @JvmStatic external fun nativeDeletePage(handle: Long, index: Int)
    @JvmStatic external fun nativeRenderPageThumbnail(handle: Long, pageIndex: Int, bitmap: android.graphics.Bitmap, clearBackground: Boolean = true): Boolean
    @JvmStatic external fun nativeGetPageExportJson(handle: Long, pageIndex: Int): String
    @JvmStatic external fun nativeSetActivePage(handle: Long, index: Int)
    @JvmStatic external fun nativeClearActivePage(handle: Long)
    @JvmStatic external fun nativeSetPageBackgroundColor(handle: Long, argb: Int)
    @JvmStatic external fun nativeSetPageGridType(handle: Long, gridType: Int)
    @JvmStatic external fun nativeSetAllPagesBackground(handle: Long, argb: Int, gridType: Int)
    @JvmStatic external fun nativeSetPageBackgroundTexture(handle: Long, pageIndex: Int, textureId: Int, imagePath: String = "")
    @JvmStatic external fun nativeGetPageBackgroundTexture(handle: Long, pageIndex: Int): Int
    @JvmStatic external fun nativeGetPageBackgroundPath(handle: Long, pageIndex: Int): String
    @JvmStatic external fun nativeAddImageToPage(handle: Long, pageIndex: Int, path: String, textureId: Int, posX: Float, posY: Float, width: Float, height: Float): Int
    @JvmStatic external fun nativeSetPageDimensions(handle: Long, pageIndex: Int, width: Float, height: Float)
    @JvmStatic external fun nativeSetAllPagesDimensions(handle: Long, width: Float, height: Float)
    @JvmStatic external fun nativeResetDocumentPages(handle: Long, count: Int, width: Float, height: Float)
    @JvmStatic external fun nativeInsertPages(handle: Long, insertIndex: Int, count: Int, width: Float, height: Float)
    @JvmStatic external fun nativeIsDocumentEmpty(handle: Long): Boolean
    @JvmStatic external fun nativeGetPageCount(handle: Long): Int
    @JvmStatic external fun nativeGetActivePageIndex(handle: Long): Int
    @JvmStatic external fun nativeReorderPage(handle: Long, fromIdx: Int, toIdx: Int)

    // ── Undo/Redo ─────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeUndo(handle: Long)
    @JvmStatic external fun nativeRedo(handle: Long)
    @JvmStatic external fun nativeCanUndo(handle: Long): Boolean
    @JvmStatic external fun nativeCanRedo(handle: Long): Boolean

    // ── Camera ────────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeFitPageToScreen(handle: Long)
    @JvmStatic external fun nativeZoomTo(handle: Long, factor: Float, focusX: Float, focusY: Float)

    // ── Recovery ──────────────────────────────────────────────────────────────
    @JvmStatic external fun nativeHasRecovery(handle: Long): Boolean
    @JvmStatic external fun nativeRecoverSession(handle: Long): Boolean
    @JvmStatic external fun nativeDiscardRecovery(handle: Long)
}

object ToolType {
    const val SELECTION      = -1
    const val HAND           = -2
    const val SHAPE          = -3
    const val PEN            = 0
    const val BRUSH          = 1
    const val HIGHLIGHTER    = 2
    const val ERASER_PIXEL   = 3
    const val ERASER_STROKE  = 4
    const val LASER          = 5
    const val STAMP          = 6
    const val AI_RECOGNIZER  = 7
}

object ShapeType {
    // 2D Shapes
    const val LINE           = 0
    const val ARROW          = 1
    const val RECTANGLE      = 2
    const val CIRCLE         = 3
    const val TRIANGLE       = 4
    const val RIGHT_TRIANGLE = 5
    const val DIAMOND        = 6
    const val STAR           = 7
    const val HEXAGON        = 8

    // 3D Shapes
    const val CUBE           = 100
    const val CUBOID         = 101
    const val SPHERE         = 102
    const val CYLINDER       = 103
    const val CONE           = 104
    const val FRUSTUM        = 105
    const val PYRAMID        = 106
    const val PRISM          = 107
}

object TouchPhase {
    const val BEGAN  = 0
    const val MOVED  = 1
    const val ENDED  = 2
    const val CANCEL = 3
}
