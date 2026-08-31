package com.cruse.openwhiteboard.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.Rect
import android.graphics.RectF
import android.util.AttributeSet
import android.util.Log
import android.view.Choreographer
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import com.cruse.openwhiteboard.engine.EngineCallbacksInterface
import com.cruse.openwhiteboard.engine.EngineJNI
import com.cruse.openwhiteboard.engine.TouchPhase
import org.json.JSONArray
import org.json.JSONObject

/**
 * [WhiteboardSurfaceView] — Primary rendering surface for OpenWhiteBoard.
 */
class WhiteboardSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : SurfaceView(context, attrs),
    SurfaceHolder.Callback,
    DefaultLifecycleObserver {

    companion object {
        private const val TAG = "WhiteboardSurfaceView"
    }

    private var engineHandle: Long = 0L
    private var surfaceReady = false

    private val choreographer by lazy { Choreographer.getInstance() }
    private var frameCallbackPosted = false
    private var renderingEnabled = false

    private val frameCallback = Choreographer.FrameCallback {
        frameCallbackPosted = false
        if (renderingEnabled && surfaceReady && engineHandle != 0L) {
            try {
                EngineJNI.nativeRenderFrame(engineHandle)
                scheduleNextFrame()
            } catch (e: Throwable) {
                Log.e(TAG, "Error in nativeRenderFrame", e)
            }
        }
    }

    var engineCallbacks: EngineCallbacksInterface? = null

    private val MAX_POINTERS = 10
    private val ids       = IntArray(MAX_POINTERS)
    private val xs        = FloatArray(MAX_POINTERS)
    private val ys        = FloatArray(MAX_POINTERS)
    private val pressures = FloatArray(MAX_POINTERS)
    private val majors    = FloatArray(MAX_POINTERS)
    private val phases    = IntArray(MAX_POINTERS)

    init {
        holder.addCallback(this)
        setZOrderOnTop(false)
        setZOrderMediaOverlay(false)
    }

    fun initEngine(filesDir: String) {
        if (engineHandle != 0L) return
        if (!EngineJNI.isNativeLoaded) {
            Log.e(TAG, "Cannot init engine: native library not loaded")
            return
        }

        try {
            engineHandle = EngineJNI.nativeCreate(filesDir)

            val cbs = object : EngineCallbacksInterface {
                override fun onInvalidate() {
                    scheduleNextFrame()
                }
                override fun onDocumentDirtyChanged(isDirty: Boolean) {
                    post { engineCallbacks?.onDocumentDirtyChanged(isDirty) }
                }
                override fun onRecoveryAvailable(snapshotPath: String, pageCount: Int, timestamp: Long) {
                    post { engineCallbacks?.onRecoveryAvailable(snapshotPath, pageCount, timestamp) }
                }
                override fun onAutoSaveComplete(success: Boolean) {
                    post { engineCallbacks?.onAutoSaveComplete(success) }
                }
                override fun onPagesChanged() {
                    post { engineCallbacks?.onPagesChanged() }
                }
                override fun onUndoRedoChanged(canUndo: Boolean, canRedo: Boolean) {
                    post { engineCallbacks?.onUndoRedoChanged(canUndo, canRedo) }
                }
                override fun onError(message: String) {
                    post { engineCallbacks?.onError(message) }
                }
                override fun onSelectionChanged(hasSelection: Boolean, isLocked: Boolean, left: Float, top: Float, right: Float, bottom: Float) {
                    post { engineCallbacks?.onSelectionChanged(hasSelection, isLocked, left, top, right, bottom) }
                }
            }
            EngineJNI.nativeSetCallbacks(engineHandle, cbs)
            Log.i(TAG, "Engine created with handle=$engineHandle")
        } catch (e: Throwable) {
            Log.e(TAG, "Failed to create native engine", e)
        }
    }

    fun destroyEngine() {
        renderingEnabled = false
        if (engineHandle != 0L) {
            try {
                EngineJNI.nativeDestroy(engineHandle)
            } catch (e: Throwable) {
                Log.e(TAG, "Error destroying engine", e)
            }
            engineHandle = 0L
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (engineHandle == 0L) return
        val surface: Surface = holder.surface
        var w = holder.surfaceFrame.width()
        var h = holder.surfaceFrame.height()
        if (w <= 0) w = if (width > 0) width else 1920
        if (h <= 0) h = if (height > 0) height else 1080

        try {
            val ok = EngineJNI.nativeSurfaceCreated(engineHandle, surface, w, h)
            if (ok) {
                surfaceReady = true
                renderingEnabled = true
                ensurePageTextureLoaded(getActivePageIndex())
                scheduleNextFrame()
            }
        } catch (e: Throwable) {
            Log.e(TAG, "Error in nativeSurfaceCreated", e)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, w: Int, h: Int) {
        if (engineHandle != 0L && surfaceReady && w > 0 && h > 0) {
            try {
                EngineJNI.nativeSurfaceChanged(engineHandle, w, h)
            } catch (e: Throwable) {
                Log.e(TAG, "Error in nativeSurfaceChanged", e)
            }
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        renderingEnabled = false
        surfaceReady = false
        if (engineHandle != 0L) {
            try {
                EngineJNI.nativeSurfaceDestroyed(engineHandle)
            } catch (e: Throwable) {
                Log.e(TAG, "Error in nativeSurfaceDestroyed", e)
            }
        }
    }

    fun isSurfaceReady(): Boolean = surfaceReady && engineHandle != 0L

    fun runWhenSurfaceReady(action: () -> Unit) {
        if (isSurfaceReady()) {
            action()
        } else {
            postDelayed({
                if (isSurfaceReady()) {
                    action()
                } else {
                    postDelayed({ action() }, 150)
                }
            }, 100)
        }
    }

    private fun scheduleNextFrame() {
        if (!frameCallbackPosted && renderingEnabled) {
            frameCallbackPosted = true
            try {
                choreographer.postFrameCallback(frameCallback)
            } catch (e: Throwable) {
                Log.e(TAG, "Failed to post frame callback to Choreographer", e)
                frameCallbackPosted = false
            }
        }
    }

    var onCanvasTouchListener: (() -> Unit)? = null
    /** Fires on a quick tap (< 350ms, minimal movement) — used for tap-to-select. */
    var onCanvasTapListener: ((screenX: Float, screenY: Float) -> Unit)? = null
    /** Fires on a long-press (>= 500ms, minimal movement) — used for paste popup. */
    var onCanvasLongPressListener: ((screenX: Float, screenY: Float) -> Unit)? = null

    private var touchDownX = 0f
    private var touchDownY = 0f
    private var touchDownTime = 0L
    private var isPotentialTap = false
    private var longPressTriggered = false
    private val longPressHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private var longPressRunnable: Runnable? = null

    private val LONG_PRESS_MS = 500L
    private val TAP_MAX_MS = 350L
    private val TAP_MAX_DIST_SQ = 20f * 20f

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (engineHandle == 0L || !surfaceReady) return false

        val pointerCount = minOf(event.pointerCount, MAX_POINTERS)
        val action = event.actionMasked
        val actionIndex = event.actionIndex
        val isPen = event.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS

        if (action == MotionEvent.ACTION_DOWN) {
            touchDownX = event.x
            touchDownY = event.y
            touchDownTime = System.currentTimeMillis()
            isPotentialTap = true
            longPressTriggered = false

            // Schedule long-press callback
            val lx = event.x
            val ly = event.y
            longPressRunnable?.let { longPressHandler.removeCallbacks(it) }
            longPressRunnable = Runnable {
                if (isPotentialTap) {
                    longPressTriggered = true
                    isPotentialTap = false  // cancel tap recognition
                    onCanvasLongPressListener?.invoke(lx, ly)
                }
            }.also { longPressHandler.postDelayed(it, LONG_PRESS_MS) }

            // Tell Android's input system to stop batching events and deliver
            // MotionEvents immediately — this is the single biggest latency fix
            // for making the stroke appear exactly under the pen tip.
            requestUnbufferedDispatch(event)
            onCanvasTouchListener?.invoke()
        } else if (action == MotionEvent.ACTION_MOVE) {
            val dx = event.x - touchDownX
            val dy = event.y - touchDownY
            if (dx * dx + dy * dy > TAP_MAX_DIST_SQ) {
                // Finger moved — cancel both tap and long-press
                isPotentialTap = false
                longPressRunnable?.let { longPressHandler.removeCallbacks(it) }
                longPressRunnable = null
            }
        } else if (action == MotionEvent.ACTION_UP) {
            longPressRunnable?.let { longPressHandler.removeCallbacks(it) }
            longPressRunnable = null
            val dx = event.x - touchDownX
            val dy = event.y - touchDownY
            val dt = System.currentTimeMillis() - touchDownTime
            // Fire tap only if quick, minimal movement, and long-press didn't already fire
            if (isPotentialTap && !longPressTriggered && dx * dx + dy * dy <= TAP_MAX_DIST_SQ && dt < TAP_MAX_MS) {
                onCanvasTapListener?.invoke(event.x, event.y)
            }
            isPotentialTap = false
        } else if (action == MotionEvent.ACTION_CANCEL) {
            longPressRunnable?.let { longPressHandler.removeCallbacks(it) }
            longPressRunnable = null
            isPotentialTap = false
        }

        // Schedule a frame immediately so the first point renders this frame,
        // not the next scheduled Choreographer vsync tick (~16ms later).
        if (!frameCallbackPosted && renderingEnabled && surfaceReady) {
            scheduleNextFrame()
        }

        // 1. Process Historical Touch Batch (120Hz / 240Hz touch sampling for ultra-smooth strokes)
        if (action == MotionEvent.ACTION_MOVE && event.historySize > 0) {
            for (h in 0 until event.historySize) {
                for (i in 0 until pointerCount) {
                    ids[i]       = event.getPointerId(i)
                    xs[i]        = event.getHistoricalX(i, h)
                    ys[i]        = event.getHistoricalY(i, h)
                    pressures[i] = event.getHistoricalPressure(i, h).coerceIn(0.01f, 1f)
                    majors[i]    = event.getHistoricalTouchMajor(i, h)
                    phases[i]    = TouchPhase.MOVED
                }
                val histTime = event.getHistoricalEventTime(h) * 1_000_000L
                try {
                    EngineJNI.nativeTouchEvent(
                        engineHandle, actionIndex, isPen,
                        ids, xs, ys, pressures, majors, phases, histTime
                    )
                } catch (e: Throwable) {
                    Log.e(TAG, "Error in historical nativeTouchEvent", e)
                }
            }
        }

        // 2. Process Current Touch Event
        for (i in 0 until pointerCount) {
            ids[i]       = event.getPointerId(i)
            xs[i]        = event.getX(i)
            ys[i]        = event.getY(i)
            pressures[i] = event.getPressure(i).coerceIn(0.01f, 1f)
            majors[i]    = event.getTouchMajor(i)

            phases[i] = when {
                action == MotionEvent.ACTION_DOWN && i == actionIndex         -> TouchPhase.BEGAN
                action == MotionEvent.ACTION_POINTER_DOWN && i == actionIndex -> TouchPhase.BEGAN
                action == MotionEvent.ACTION_UP && i == actionIndex           -> TouchPhase.ENDED
                action == MotionEvent.ACTION_POINTER_UP && i == actionIndex   -> TouchPhase.ENDED
                action == MotionEvent.ACTION_CANCEL                           -> TouchPhase.CANCEL
                else                                                           -> TouchPhase.MOVED
            }
        }

        try {
            EngineJNI.nativeTouchEvent(
                engineHandle,
                actionIndex,
                isPen,
                ids,
                xs,
                ys,
                pressures,
                majors,
                phases,
                event.eventTime * 1_000_000L
            )
        } catch (e: Throwable) {
            Log.e(TAG, "Error in nativeTouchEvent", e)
        }
        return true
    }

    fun attachToLifecycle(owner: LifecycleOwner) {
        owner.lifecycle.addObserver(this)
    }

    override fun onResume(owner: LifecycleOwner) {
        renderingEnabled = true
        if (surfaceReady) scheduleNextFrame()
    }

    override fun onPause(owner: LifecycleOwner) {
        renderingEnabled = false
    }

    override fun onDestroy(owner: LifecycleOwner) {
        destroyEngine()
    }

    fun setActiveTool(toolIndex: Int) {
        if (engineHandle != 0L) EngineJNI.nativeSetActiveTool(engineHandle, toolIndex)
    }

    fun setStrokeColor(argb: Int) {
        if (engineHandle != 0L) EngineJNI.nativeSetStrokeColor(engineHandle, argb)
    }

    fun setStrokeWidth(width: Float) {
        if (engineHandle != 0L) EngineJNI.nativeSetStrokeWidth(engineHandle, width)
    }

    fun setStrokeOpacity(opacity: Float) {
        if (engineHandle != 0L) EngineJNI.nativeSetStrokeOpacity(engineHandle, opacity.coerceIn(0f, 1f))
    }

    fun setPenType(penType: Int) {
        if (engineHandle != 0L) EngineJNI.nativeSetPenType(engineHandle, penType)
    }

    fun setEraserSize(size: Float) {
        if (engineHandle != 0L) EngineJNI.nativeSetEraserSize(engineHandle, size)
    }

    fun setEraserMode(mode: Int) {
        if (engineHandle != 0L) EngineJNI.nativeSetEraserMode(engineHandle, mode)
    }

    fun setShapeType(shapeType: Int) {
        if (engineHandle != 0L) EngineJNI.nativeSetShapeType(engineHandle, shapeType)
    }

    fun undo() { if (engineHandle != 0L) EngineJNI.nativeUndo(engineHandle) }
    fun redo() { if (engineHandle != 0L) EngineJNI.nativeRedo(engineHandle) }

    private val pageSlidePaths = java.util.concurrent.ConcurrentHashMap<Int, String>()
    private val slideBitmapCache = object : android.util.LruCache<Int, Bitmap>(64) {
        override fun sizeOf(key: Int, value: Bitmap): Int = 1
    }

    fun setPageSlidePath(pageIndex: Int, path: String) {
        if (path.isNotBlank()) {
            pageSlidePaths[pageIndex] = path
            slideBitmapCache.remove(pageIndex)
            if (engineHandle != 0L) {
                EngineJNI.nativeSetPageBackgroundTexture(engineHandle, pageIndex, 0, path)
            }
        }
    }

    fun getPageSlidePath(pageIndex: Int): String? = pageSlidePaths[pageIndex]

    fun clearPageSlidePaths() {
        pageSlidePaths.clear()
        slideBitmapCache.evictAll()
    }

    fun addPage()                  { if (engineHandle != 0L) { EngineJNI.nativeAddPage(engineHandle); scheduleNextFrame() } }
    fun insertPage(index: Int)     {
        if (engineHandle != 0L) {
            val count = getPageCount()
            for (i in count downTo (index + 1)) {
                val p = pageSlidePaths.remove(i - 1)
                if (p != null) pageSlidePaths[i] = p
            }
            slideBitmapCache.evictAll()
            EngineJNI.nativeInsertPage(engineHandle, index)
            scheduleNextFrame()
        }
    }
    fun duplicatePage(index: Int)  {
        if (engineHandle != 0L) {
            val srcPath = pageSlidePaths[index]
            val count = getPageCount()
            for (i in count downTo (index + 2)) {
                val p = pageSlidePaths.remove(i - 1)
                if (p != null) pageSlidePaths[i] = p
            }
            if (srcPath != null) pageSlidePaths[index + 1] = srcPath
            slideBitmapCache.evictAll()
            EngineJNI.nativeDuplicatePage(engineHandle, index)
            scheduleNextFrame()
        }
    }
    fun deletePage(index: Int)     {
        if (engineHandle != 0L) {
            pageSlidePaths.remove(index)
            slideBitmapCache.remove(index)
            val count = getPageCount()
            for (i in (index + 1)..count) {
                val p = pageSlidePaths.remove(i)
                if (p != null) pageSlidePaths[i - 1] = p
            }
            slideBitmapCache.evictAll()
            EngineJNI.nativeDeletePage(engineHandle, index)
            scheduleNextFrame()
        }
    }
    fun reorderPage(fromIdx: Int, toIdx: Int) {
        if (engineHandle != 0L) {
            val fromPath = pageSlidePaths.remove(fromIdx)
            if (fromIdx < toIdx) {
                for (i in fromIdx until toIdx) {
                    val p = pageSlidePaths.remove(i + 1)
                    if (p != null) pageSlidePaths[i] = p
                }
            } else if (fromIdx > toIdx) {
                for (i in fromIdx downTo (toIdx + 1)) {
                    val p = pageSlidePaths.remove(i - 1)
                    if (p != null) pageSlidePaths[i] = p
                }
            }
            if (fromPath != null) pageSlidePaths[toIdx] = fromPath
            slideBitmapCache.evictAll()
            EngineJNI.nativeReorderPage(engineHandle, fromIdx, toIdx)
            scheduleNextFrame()
        }
    }

    fun renderPageThumbnail(pageIndex: Int, bitmap: Bitmap): Boolean {
        if (engineHandle == 0L) return false
        val slidePath = pageSlidePaths[pageIndex]
        var hasBackgroundDrawn = false
        if (!slidePath.isNullOrBlank()) {
            try {
                val slideFile = java.io.File(slidePath)
                if (slideFile.exists() && slideFile.length() > 0L) {
                    var slideBmp = slideBitmapCache.get(pageIndex)
                    if (slideBmp == null || slideBmp.isRecycled) {
                        val boundsOpts = BitmapFactory.Options().apply {
                            inJustDecodeBounds = true
                        }
                        BitmapFactory.decodeFile(slideFile.absolutePath, boundsOpts)
                        val sample = if (boundsOpts.outWidth > 0 && bitmap.width > 0) {
                            maxOf(1, boundsOpts.outWidth / bitmap.width)
                        } else 1
                        val decodeOpts = BitmapFactory.Options().apply {
                            inSampleSize = sample
                            inPreferredConfig = Bitmap.Config.RGB_565
                        }
                        val fullBmp = BitmapFactory.decodeFile(slideFile.absolutePath, decodeOpts)
                        if (fullBmp != null) {
                            slideBmp = Bitmap.createScaledBitmap(fullBmp, bitmap.width, bitmap.height, true)
                            if (slideBmp != fullBmp && !fullBmp.isRecycled) {
                                fullBmp.recycle()
                            }
                            slideBitmapCache.put(pageIndex, slideBmp)
                        }
                    }
                    if (slideBmp != null && !slideBmp.isRecycled) {
                        val canvas = Canvas(bitmap)
                        val srcRect = Rect(0, 0, slideBmp.width, slideBmp.height)
                        val dstRect = Rect(0, 0, bitmap.width, bitmap.height)
                        val paint = Paint(Paint.FILTER_BITMAP_FLAG)
                        canvas.drawBitmap(slideBmp, srcRect, dstRect, paint)
                        hasBackgroundDrawn = true
                    }
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Error drawing slide thumb for page $pageIndex", e)
            }
        }
        return EngineJNI.nativeRenderPageThumbnail(engineHandle, pageIndex, bitmap, !hasBackgroundDrawn)
    }

    /**
     * Renders full-resolution vector strokes, vector shapes, embedded images,
     * and crisp background images directly into an Android [Canvas] (such as [android.graphics.pdf.PdfDocument] canvas).
     */
    fun renderPageToVectorCanvas(pageIndex: Int, canvas: Canvas, targetWidth: Float, targetHeight: Float) {
        if (engineHandle == 0L) return
        val jsonStr = EngineJNI.nativeGetPageExportJson(engineHandle, pageIndex)
        if (jsonStr.isBlank() || jsonStr == "{}") return

        try {
            val root = JSONObject(jsonStr)
            val origW = root.optDouble("width", 1920.0).toFloat().coerceAtLeast(1f)
            val origH = root.optDouble("height", 1080.0).toFloat().coerceAtLeast(1f)

            val scaleX = targetWidth / origW
            val scaleY = targetHeight / origH
            val avgScale = (scaleX + scaleY) * 0.5f

            // 1. Background
            val bgObj = root.optJSONObject("background")
            val bgColor = bgObj?.optLong("color", 0xFFFFFFFFL)?.toInt() ?: 0xFFFFFFFF.toInt()
            val bgGridType = bgObj?.optInt("gridType", 0) ?: 0
            var bgImagePath = bgObj?.optString("imagePath", "") ?: ""
            if (bgImagePath.isBlank()) {
                bgImagePath = pageSlidePaths[pageIndex] ?: ""
            }

            // Draw solid background color
            val bgPaint = Paint().apply {
                color = bgColor
                style = Paint.Style.FILL
            }
            canvas.drawRect(0f, 0f, targetWidth, targetHeight, bgPaint)

            // Draw slide/background image if available
            if (bgImagePath.isNotBlank()) {
                try {
                    val slideFile = java.io.File(bgImagePath)
                    if (slideFile.exists() && slideFile.length() > 0L) {
                        val opts = BitmapFactory.Options().apply {
                            inPreferredConfig = Bitmap.Config.ARGB_8888
                            inScaled = false
                        }
                        val slideBmp = BitmapFactory.decodeFile(slideFile.absolutePath, opts)
                        if (slideBmp != null) {
                            val imgPaint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG or Paint.DITHER_FLAG)
                            val dstRect = RectF(0f, 0f, targetWidth, targetHeight)
                            canvas.drawBitmap(slideBmp, null, dstRect, imgPaint)
                            slideBmp.recycle()
                        }
                    }
                } catch (e: Throwable) {
                    Log.e(TAG, "Error drawing background slide for page $pageIndex", e)
                }
            }

            // Draw Grid / Lines / Dots if enabled
            if (bgGridType > 0) {
                val gridPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                    color = 0x22000000 // subtle light gray
                    strokeWidth = 1.5f * avgScale
                    style = Paint.Style.STROKE
                }
                val spacing = 40f * avgScale
                when (bgGridType) {
                    1 -> { // Grid
                        var x = spacing
                        while (x < targetWidth) {
                            canvas.drawLine(x, 0f, x, targetHeight, gridPaint)
                            x += spacing
                        }
                        var y = spacing
                        while (y < targetHeight) {
                            canvas.drawLine(0f, y, targetWidth, y, gridPaint)
                            y += spacing
                        }
                    }
                    2 -> { // Ruled Lines
                        var y = spacing
                        while (y < targetHeight) {
                            canvas.drawLine(0f, y, targetWidth, y, gridPaint)
                            y += spacing
                        }
                    }
                    3 -> { // Dots
                        gridPaint.style = Paint.Style.FILL
                        val dotRad = 2.0f * avgScale
                        var x = spacing
                        while (x < targetWidth) {
                            var y = spacing
                            while (y < targetHeight) {
                                canvas.drawCircle(x, y, dotRad, gridPaint)
                                y += spacing
                            }
                            x += spacing
                        }
                    }
                }
            }

            // 2. Embedded Images
            val imagesArr = root.optJSONArray("images")
            if (imagesArr != null) {
                for (i in 0 until imagesArr.length()) {
                    val imgObj = imagesArr.optJSONObject(i) ?: continue
                    val path = imgObj.optString("path", "")
                    if (path.isBlank()) continue
                    val f = java.io.File(path)
                    if (!f.exists() || f.length() == 0L) continue

                    try {
                        val opts = BitmapFactory.Options().apply {
                            inPreferredConfig = Bitmap.Config.ARGB_8888
                            inScaled = false
                        }
                        val bmp = BitmapFactory.decodeFile(f.absolutePath, opts) ?: continue
                        val l = imgObj.optDouble("left", 0.0).toFloat() * scaleX
                        val t = imgObj.optDouble("top", 0.0).toFloat() * scaleY
                        val r = imgObj.optDouble("right", 0.0).toFloat() * scaleX
                        val b = imgObj.optDouble("bottom", 0.0).toFloat() * scaleY
                        val opacity = imgObj.optDouble("opacity", 1.0).toFloat()
                        val rotation = imgObj.optDouble("rotation", 0.0).toFloat()

                        val dstRect = RectF(l, t, r, b)
                        val p = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG or Paint.DITHER_FLAG).apply {
                            alpha = (opacity * 255).toInt().coerceIn(0, 255)
                        }

                        if (rotation != 0f) {
                            canvas.save()
                            canvas.rotate(rotation, dstRect.centerX(), dstRect.centerY())
                            canvas.drawBitmap(bmp, null, dstRect, p)
                            canvas.restore()
                        } else {
                            canvas.drawBitmap(bmp, null, dstRect, p)
                        }
                        bmp.recycle()
                    } catch (t: Throwable) {
                        Log.e(TAG, "Error drawing image element in export", t)
                    }
                }
            }

            // 3. Shapes
            val shapesArr = root.optJSONArray("shapes")
            if (shapesArr != null) {
                for (i in 0 until shapesArr.length()) {
                    val shObj = shapesArr.optJSONObject(i) ?: continue
                    val type = shObj.optInt("type", 0)
                    val l = shObj.optDouble("left", 0.0).toFloat() * scaleX
                    val t = shObj.optDouble("top", 0.0).toFloat() * scaleY
                    val r = shObj.optDouble("right", 0.0).toFloat() * scaleX
                    val b = shObj.optDouble("bottom", 0.0).toFloat() * scaleY
                    val sColor = shObj.optLong("strokeColor", 0xFF000000L).toInt()
                    val fColor = shObj.optLong("fillColor", 0x00000000L).toInt()
                    val sWidth = shObj.optDouble("strokeWidth", 4.0).toFloat() * avgScale
                    val rotation = shObj.optDouble("rotation", 0.0).toFloat()

                    val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                        color = fColor
                        style = Paint.Style.FILL
                    }
                    val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                        color = sColor
                        style = Paint.Style.STROKE
                        strokeWidth = sWidth
                        strokeCap = Paint.Cap.ROUND
                        strokeJoin = Paint.Join.ROUND
                    }

                    val hasFill = (fColor ushr 24) > 0
                    val hasStroke = (sColor ushr 24) > 0 && sWidth > 0f

                    val rect = RectF(minOf(l, r), minOf(t, b), maxOf(l, r), maxOf(t, b))
                    if (rotation != 0f) {
                        canvas.save()
                        canvas.rotate(rotation, rect.centerX(), rect.centerY())
                    }

                    when (type) {
                        0 -> { // Rectangle
                            if (hasFill) canvas.drawRect(rect, fillPaint)
                            if (hasStroke) canvas.drawRect(rect, strokePaint)
                        }
                        1 -> { // Rounded Rectangle
                            val rad = minOf(rect.width(), rect.height()) * 0.15f
                            if (hasFill) canvas.drawRoundRect(rect, rad, rad, fillPaint)
                            if (hasStroke) canvas.drawRoundRect(rect, rad, rad, strokePaint)
                        }
                        2 -> { // Circle / Ellipse
                            if (hasFill) canvas.drawOval(rect, fillPaint)
                            if (hasStroke) canvas.drawOval(rect, strokePaint)
                        }
                        3 -> { // Line
                            if (hasStroke) canvas.drawLine(l, t, r, b, strokePaint)
                        }
                        4 -> { // Arrow
                            if (hasStroke) {
                                canvas.drawLine(l, t, r, b, strokePaint)
                                val dx = r - l
                                val dy = b - t
                                val angle = Math.atan2(dy.toDouble(), dx.toDouble()).toFloat()
                                val arrowLen = maxOf(20f * avgScale, sWidth * 3f)
                                val arrowAngle = 0.5f
                                val arrowPath = Path().apply {
                                    moveTo(r, b)
                                    lineTo((r - arrowLen * Math.cos(angle - arrowAngle.toDouble())).toFloat(),
                                           (b - arrowLen * Math.sin(angle - arrowAngle.toDouble())).toFloat())
                                    moveTo(r, b)
                                    lineTo((r - arrowLen * Math.cos(angle + arrowAngle.toDouble())).toFloat(),
                                           (b - arrowLen * Math.sin(angle + arrowAngle.toDouble())).toFloat())
                                }
                                canvas.drawPath(arrowPath, strokePaint)
                            }
                        }
                        5 -> { // Triangle
                            val triPath = Path().apply {
                                moveTo(rect.centerX(), rect.top)
                                lineTo(rect.right, rect.bottom)
                                lineTo(rect.left, rect.bottom)
                                close()
                            }
                            if (hasFill) canvas.drawPath(triPath, fillPaint)
                            if (hasStroke) canvas.drawPath(triPath, strokePaint)
                        }
                        6 -> { // Star
                            val cx = rect.centerX()
                            val cy = rect.centerY()
                            val outerR = minOf(rect.width(), rect.height()) * 0.5f
                            val innerR = outerR * 0.4f
                            val starPath = Path()
                            for (p in 0 until 10) {
                                val rad = if (p % 2 == 0) outerR else innerR
                                val a = (p * Math.PI / 5.0) - (Math.PI / 2.0)
                                val px = (cx + rad * Math.cos(a)).toFloat()
                                val py = (cy + rad * Math.sin(a)).toFloat()
                                if (p == 0) starPath.moveTo(px, py) else starPath.lineTo(px, py)
                            }
                            starPath.close()
                            if (hasFill) canvas.drawPath(starPath, fillPaint)
                            if (hasStroke) canvas.drawPath(starPath, strokePaint)
                        }
                        else -> {
                            if (hasFill) canvas.drawRect(rect, fillPaint)
                            if (hasStroke) canvas.drawRect(rect, strokePaint)
                        }
                    }

                    if (rotation != 0f) {
                        canvas.restore()
                    }
                }
            }

            // 4. Vector Strokes
            val strokesArr = root.optJSONArray("strokes")
            if (strokesArr != null) {
                for (i in 0 until strokesArr.length()) {
                    val sObj = strokesArr.optJSONObject(i) ?: continue
                    val sColor = sObj.optLong("color", 0xFF000000L).toInt()
                    val sWidth = sObj.optDouble("width", 4.0).toFloat() * avgScale
                    val opacity = sObj.optDouble("opacity", 1.0).toFloat()
                    val penType = sObj.optInt("penType", 0)
                    val ptsArr = sObj.optJSONArray("points") ?: continue
                    val ptsCount = ptsArr.length()
                    if (ptsCount == 0) continue

                    val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.DITHER_FLAG).apply {
                        style = Paint.Style.STROKE
                        strokeCap = Paint.Cap.ROUND
                        strokeJoin = Paint.Join.ROUND
                        strokeWidth = sWidth
                        color = sColor
                        alpha = (opacity * 255).toInt().coerceIn(0, 255)
                        if (penType == 2) { // Highlighter
                            xfermode = PorterDuffXfermode(PorterDuff.Mode.SRC_OVER)
                            alpha = (opacity * 160).toInt().coerceIn(0, 255)
                        }
                    }

                    if (ptsCount == 1) {
                        val p0 = ptsArr.optJSONObject(0) ?: continue
                        val x = p0.optDouble("x", 0.0).toFloat() * scaleX
                        val y = p0.optDouble("y", 0.0).toFloat() * scaleY
                        val dotPaint = Paint(strokePaint).apply { style = Paint.Style.FILL }
                        canvas.drawCircle(x, y, maxOf(1f, sWidth * 0.5f), dotPaint)
                    } else if (ptsCount == 2) {
                        val p0 = ptsArr.optJSONObject(0) ?: continue
                        val p1 = ptsArr.optJSONObject(1) ?: continue
                        canvas.drawLine(
                            p0.optDouble("x", 0.0).toFloat() * scaleX,
                            p0.optDouble("y", 0.0).toFloat() * scaleY,
                            p1.optDouble("x", 0.0).toFloat() * scaleX,
                            p1.optDouble("y", 0.0).toFloat() * scaleY,
                            strokePaint
                        )
                    } else {
                        val strokePath = Path()
                        val p0 = ptsArr.optJSONObject(0) ?: continue
                        var prevX = p0.optDouble("x", 0.0).toFloat() * scaleX
                        var prevY = p0.optDouble("y", 0.0).toFloat() * scaleY
                        strokePath.moveTo(prevX, prevY)

                        for (j in 1 until ptsCount) {
                            val pt = ptsArr.optJSONObject(j) ?: continue
                            val curX = pt.optDouble("x", 0.0).toFloat() * scaleX
                            val curY = pt.optDouble("y", 0.0).toFloat() * scaleY
                            val midX = (prevX + curX) * 0.5f
                            val midY = (prevY + curY) * 0.5f
                            strokePath.quadTo(prevX, prevY, midX, midY)
                            prevX = curX
                            prevY = curY
                        }
                        strokePath.lineTo(prevX, prevY)
                        canvas.drawPath(strokePath, strokePaint)
                    }
                }
            }
        } catch (e: Throwable) {
            Log.e(TAG, "Error rendering vector page $pageIndex to canvas", e)
        }
    }

    fun isPageTextureLoaded(pageIndex: Int): Boolean {
        if (engineHandle == 0L) return false
        return EngineJNI.nativeGetPageBackgroundTexture(engineHandle, pageIndex) > 0
    }

    fun hasPageSlideFile(pageIndex: Int): Boolean {
        var path = pageSlidePaths[pageIndex]
        if (path.isNullOrBlank() && engineHandle != 0L) {
            path = EngineJNI.nativeGetPageBackgroundPath(engineHandle, pageIndex)
        }
        if (!path.isNullOrBlank()) {
            val file = java.io.File(path)
            return file.exists() && file.length() > 0L
        }
        return false
    }

    fun ensurePageTextureLoaded(pageIndex: Int): Boolean {
        if (engineHandle == 0L) return false
        // Fast path: if texture is already loaded in GPU, return immediately (0ms)!
        if (isPageTextureLoaded(pageIndex)) return true

        var path = pageSlidePaths[pageIndex]
        if (path.isNullOrBlank()) {
            val enginePath = EngineJNI.nativeGetPageBackgroundPath(engineHandle, pageIndex)
            if (enginePath.isNotBlank()) {
                path = enginePath
                pageSlidePaths[pageIndex] = enginePath
            }
        }
        if (!path.isNullOrBlank()) {
            try {
                val file = java.io.File(path)
                if (file.exists() && file.length() > 0L) {
                    val options = android.graphics.BitmapFactory.Options().apply {
                        inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888
                    }
                    val bitmap = android.graphics.BitmapFactory.decodeFile(file.absolutePath, options)
                    if (bitmap != null) {
                        val textureId = uploadTexture(bitmap)
                        if (textureId > 0) {
                            EngineJNI.nativeSetPageBackgroundTexture(engineHandle, pageIndex, textureId, path)
                            scheduleNextFrame()
                        }
                        if (!bitmap.isRecycled) bitmap.recycle()
                        return true
                    }
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Failed to load texture for page $pageIndex", e)
            }
        }
        return false
    }

    fun setActivePage(index: Int) {
        if (engineHandle != 0L) {
            ensurePageTextureLoaded(index)
            EngineJNI.nativeSetActivePage(engineHandle, index)
            scheduleNextFrame()
        }
    }
    fun clearActivePage()          { if (engineHandle != 0L) { EngineJNI.nativeClearActivePage(engineHandle); scheduleNextFrame() } }
    fun setPageBackgroundColor(argb: Int) { if (engineHandle != 0L) { EngineJNI.nativeSetPageBackgroundColor(engineHandle, argb); scheduleNextFrame() } }
    fun setPageGridType(gridType: Int) { if (engineHandle != 0L) { EngineJNI.nativeSetPageGridType(engineHandle, gridType); scheduleNextFrame() } }
    fun setAllPagesBackground(argb: Int, gridType: Int) { if (engineHandle != 0L) { EngineJNI.nativeSetAllPagesBackground(engineHandle, argb, gridType); scheduleNextFrame() } }
    fun getPageCount(): Int        = if (engineHandle != 0L) EngineJNI.nativeGetPageCount(engineHandle) else 0
    fun getActivePageIndex(): Int  = if (engineHandle != 0L) EngineJNI.nativeGetActivePageIndex(engineHandle) else 0
    fun getPageBackgroundPath(pageIndex: Int): String = if (engineHandle != 0L) EngineJNI.nativeGetPageBackgroundPath(engineHandle, pageIndex) else ""
    fun getPageBackgroundTexture(pageIndex: Int): Int = if (engineHandle != 0L) EngineJNI.nativeGetPageBackgroundTexture(engineHandle, pageIndex) else 0

    fun fitPageToScreen() { if (engineHandle != 0L) EngineJNI.nativeFitPageToScreen(engineHandle) }

    fun saveDocument(path: String): Boolean =
        engineHandle != 0L && EngineJNI.nativeSaveDocument(engineHandle, path)

    fun openDocument(path: String): Boolean {
        if (engineHandle == 0L) return false
        val ok = EngineJNI.nativeOpenDocument(engineHandle, path)
        if (ok) {
            clearPageSlidePaths()
            val count = getPageCount()
            for (i in 0 until count) {
                val bgPath = EngineJNI.nativeGetPageBackgroundPath(engineHandle, i)
                if (bgPath.isNotBlank()) {
                    pageSlidePaths[i] = bgPath
                }
            }
            ensurePageTextureLoaded(getActivePageIndex())
            scheduleNextFrame()
        }
        return ok
    }

    fun newDocument() {
        clearPageSlidePaths()
        if (engineHandle != 0L) {
            EngineJNI.nativeNewDocument(engineHandle)
            scheduleNextFrame()
        }
    }

    fun importImage(path: String, posX: Float? = null, posY: Float? = null): Boolean {
        if (engineHandle == 0L) return false
        val file = java.io.File(path)
        if (!file.exists() || !file.canRead()) return false

        try {
            val options = android.graphics.BitmapFactory.Options().apply {
                inJustDecodeBounds = true
            }
            android.graphics.BitmapFactory.decodeFile(path, options)
            if (options.outWidth <= 0 || options.outHeight <= 0) return false

            val maxDimension = 1920
            var sampleSize = 1
            while (options.outWidth / sampleSize > maxDimension || options.outHeight / sampleSize > maxDimension) {
                sampleSize *= 2
            }

            val decodeOptions = android.graphics.BitmapFactory.Options().apply {
                inSampleSize = sampleSize
                inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888
            }
            val bitmap = android.graphics.BitmapFactory.decodeFile(path, decodeOptions) ?: return false

            val w = bitmap.width
            val h = bitmap.height

            val byteBuffer = java.nio.ByteBuffer.allocateDirect(w * h * 4)
            byteBuffer.order(java.nio.ByteOrder.nativeOrder())
            bitmap.copyPixelsToBuffer(byteBuffer)
            byteBuffer.rewind()

            val textureId = EngineJNI.nativeUploadTexture(engineHandle, byteBuffer, w, h)
            if (textureId == 0) {
                Log.e(TAG, "Failed to upload image texture")
                bitmap.recycle()
                return false
            }

            val maxCanvasDim = 600f
            val scale = if (w > h) maxCanvasDim / w else maxCanvasDim / h
            val dstW = w * scale
            val dstH = h * scale

            val center = screenToCanvas(width * 0.5f, height * 0.5f)
            val placeX = posX ?: (center.x - dstW * 0.5f)
            val placeY = posY ?: (center.y - dstH * 0.5f)

            val imgId = EngineJNI.nativeAddImage(engineHandle, path, textureId, placeX, placeY, dstW, dstH)
            bitmap.recycle()
            scheduleNextFrame()
            Log.i(TAG, "Successfully imported image ID=$imgId ($w x $h)")
            return imgId > 0
        } catch (e: Throwable) {
            Log.e(TAG, "Error importing image: $path", e)
            return false
        }
    }

    fun uploadTexture(bitmap: Bitmap): Int {
        if (engineHandle == 0L) return 0
        val byteBuffer = java.nio.ByteBuffer.allocateDirect(bitmap.byteCount)
        byteBuffer.order(java.nio.ByteOrder.nativeOrder())
        bitmap.copyPixelsToBuffer(byteBuffer)
        byteBuffer.position(0)
        return EngineJNI.nativeUploadTexture(engineHandle, byteBuffer, bitmap.width, bitmap.height)
    }

    fun setPageBackgroundTexture(pageIndex: Int, textureId: Int, imagePath: String = "") {
        if (imagePath.isNotBlank()) {
            setPageSlidePath(pageIndex, imagePath)
        }
        if (engineHandle != 0L) {
            EngineJNI.nativeSetPageBackgroundTexture(engineHandle, pageIndex, textureId, imagePath)
            scheduleNextFrame()
        }
    }

    fun addImageToPage(pageIndex: Int, path: String, textureId: Int, posX: Float, posY: Float, width: Float, height: Float): Int {
        if (path.isNotBlank()) {
            setPageSlidePath(pageIndex, path)
        }
        if (engineHandle == 0L) return 0
        val id = EngineJNI.nativeAddImageToPage(engineHandle, pageIndex, path, textureId, posX, posY, width, height)
        scheduleNextFrame()
        return id
    }

    fun setPageDimensions(pageIndex: Int, width: Float, height: Float) {
        if (engineHandle != 0L) {
            EngineJNI.nativeSetPageDimensions(engineHandle, pageIndex, width, height)
            scheduleNextFrame()
        }
    }

    fun setAllPagesDimensions(width: Float, height: Float) {
        if (engineHandle != 0L) {
            EngineJNI.nativeSetAllPagesDimensions(engineHandle, width, height)
            scheduleNextFrame()
        }
    }

    fun resetDocumentPages(count: Int, width: Float, height: Float) {
        clearPageSlidePaths()
        if (engineHandle != 0L) {
            EngineJNI.nativeResetDocumentPages(engineHandle, count, width, height)
            scheduleNextFrame()
        }
    }

    fun insertPages(startIndex: Int, count: Int, width: Float, height: Float) {
        if (engineHandle != 0L && count > 0) {
            val total = getPageCount()
            for (i in (total - 1) downTo startIndex) {
                val p = pageSlidePaths.remove(i)
                if (p != null) {
                    pageSlidePaths[i + count] = p
                }
            }
            slideBitmapCache.evictAll()
            EngineJNI.nativeInsertPages(engineHandle, startIndex, count, width, height)
            scheduleNextFrame()
        }
    }

    fun isDocumentEmpty(): Boolean {
        if (engineHandle != 0L) {
            return EngineJNI.nativeIsDocumentEmpty(engineHandle)
        }
        return true
    }

    fun exportPdf(path: String, pageIndices: List<Int>? = null): Boolean {
        if (engineHandle == 0L) return false
        val totalCount = getPageCount()
        if (totalCount <= 0) return false
        val pagesToExport = pageIndices?.filter { it in 0 until totalCount } ?: (0 until totalCount).toList()
        if (pagesToExport.isEmpty()) return false

        val pdfDocument = android.graphics.pdf.PdfDocument()
        try {
            for ((outIdx, pageIdx) in pagesToExport.withIndex()) {
                val jsonStr = EngineJNI.nativeGetPageExportJson(engineHandle, pageIdx)
                val json = try { JSONObject(jsonStr) } catch (_: Throwable) { null }
                val pageW = (json?.optDouble("width", 1920.0)?.toInt() ?: 1920).coerceAtLeast(1)
                val pageH = (json?.optDouble("height", 1080.0)?.toInt() ?: 1080).coerceAtLeast(1)

                val pageInfo = android.graphics.pdf.PdfDocument.PageInfo.Builder(pageW, pageH, outIdx + 1).create()
                val pdfPage = pdfDocument.startPage(pageInfo)

                // Render vector strokes, vector shapes, embedded images, and original high-res slides directly into PDF canvas
                renderPageToVectorCanvas(pageIdx, pdfPage.canvas, pageW.toFloat(), pageH.toFloat())

                pdfDocument.finishPage(pdfPage)
            }
            val file = java.io.File(path)
            file.parentFile?.mkdirs()
            file.outputStream().buffered().use { out ->
                pdfDocument.writeTo(out)
            }
            return file.exists() && file.length() > 0L
        } catch (e: Throwable) {
            Log.e(TAG, "Error exporting PDF to $path", e)
            return false
        } finally {
            try { pdfDocument.close() } catch (t: Throwable) {}
        }
    }

    fun hasRecovery(): Boolean =
        engineHandle != 0L && EngineJNI.nativeHasRecovery(engineHandle)

    fun recoverSession(): Boolean =
        engineHandle != 0L && EngineJNI.nativeRecoverSession(engineHandle)

    fun discardRecovery() {
        if (engineHandle != 0L) EngineJNI.nativeDiscardRecovery(engineHandle)
    }

    fun deleteSelected()      { if (engineHandle != 0L) { EngineJNI.nativeDeleteSelected(engineHandle); scheduleNextFrame() } }
    fun flipHorizontalSelected() { if (engineHandle != 0L) { EngineJNI.nativeFlipHorizontalSelected(engineHandle); scheduleNextFrame() } }
    fun flipVerticalSelected()   { if (engineHandle != 0L) { EngineJNI.nativeFlipVerticalSelected(engineHandle); scheduleNextFrame() } }
    fun bringToFrontSelected()   { if (engineHandle != 0L) { EngineJNI.nativeBringToFrontSelected(engineHandle); scheduleNextFrame() } }
    fun sendBackwardSelected()   { if (engineHandle != 0L) { EngineJNI.nativeSendBackwardSelected(engineHandle); scheduleNextFrame() } }
    fun sendToBackSelected()     { if (engineHandle != 0L) { EngineJNI.nativeSendToBackSelected(engineHandle); scheduleNextFrame() } }
    fun bringForwardSelected()   { if (engineHandle != 0L) { EngineJNI.nativeBringForwardSelected(engineHandle); scheduleNextFrame() } }
    fun duplicateSelected()   { if (engineHandle != 0L) { EngineJNI.nativeDuplicateSelected(engineHandle); scheduleNextFrame() } }
    fun copySelected()        { if (engineHandle != 0L) { EngineJNI.nativeCopySelected(engineHandle) } }
    fun paste()               { if (engineHandle != 0L) { EngineJNI.nativePaste(engineHandle); scheduleNextFrame() } }
    fun pasteAt(x: Float, y: Float) { if (engineHandle != 0L) { EngineJNI.nativePasteAt(engineHandle, x, y); scheduleNextFrame() } }
    fun hasClipboard(): Boolean = if (engineHandle != 0L) EngineJNI.nativeHasClipboard(engineHandle) else false
    fun setSelectedColor(color: Int) { if (engineHandle != 0L) { EngineJNI.nativeSetSelectedColor(engineHandle, color); scheduleNextFrame() } }
    fun getSelectedColor(): Int      = if (engineHandle != 0L) EngineJNI.nativeGetSelectedColor(engineHandle) else 0xFF000000.toInt()
    fun toggleFillSelected()         { if (engineHandle != 0L) { EngineJNI.nativeToggleFillSelected(engineHandle); scheduleNextFrame() } }
    fun hasSelectedFilledShape(): Boolean = if (engineHandle != 0L) EngineJNI.nativeHasSelectedFilledShape(engineHandle) else false
    fun hasSelectedShape(): Boolean       = if (engineHandle != 0L) EngineJNI.nativeHasSelectedShape(engineHandle) else false
    fun lockSelected()        { if (engineHandle != 0L) { EngineJNI.nativeLockSelected(engineHandle); scheduleNextFrame() } }
    fun clearSelection()      { if (engineHandle != 0L) { EngineJNI.nativeClearSelection(engineHandle); scheduleNextFrame() } }
    /**
     * Attempt to select an object by tapping at a screen coordinate.
     * Returns true if any object was hit.
     */
    fun tapSelectAt(screenX: Float, screenY: Float): Boolean {
        if (engineHandle == 0L) return false
        return EngineJNI.nativeTapSelectAt(engineHandle, screenX, screenY)
    }

    fun canvasToScreenRect(left: Float, top: Float, right: Float, bottom: Float): android.graphics.RectF {
        if (engineHandle != 0L) {
            val res = EngineJNI.nativeCanvasToScreenRect(engineHandle, left, top, right, bottom)
            if (res != null && res.size == 4) {
                return android.graphics.RectF(res[0], res[1], res[2], res[3])
            }
        }
        return android.graphics.RectF(left, top, right, bottom)
    }

    fun screenToCanvas(screenX: Float, screenY: Float): android.graphics.PointF {
        if (engineHandle != 0L) {
            val res = EngineJNI.nativeScreenToCanvas(engineHandle, screenX, screenY)
            if (res != null && res.size == 2) {
                return android.graphics.PointF(res[0], res[1])
            }
        }
        return android.graphics.PointF(screenX, screenY)
    }
}
