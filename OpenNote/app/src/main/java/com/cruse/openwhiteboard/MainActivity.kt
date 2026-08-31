package com.cruse.openwhiteboard

import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import androidx.appcompat.app.AlertDialog
import android.content.res.ColorStateList
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Rect
import android.graphics.pdf.PdfRenderer
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.provider.Settings
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RelativeLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SwitchCompat
import androidx.core.content.FileProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.cruse.openwhiteboard.engine.EngineCallbacksInterface
import com.cruse.openwhiteboard.engine.ShapeType
import com.cruse.openwhiteboard.engine.ToolType
import com.cruse.openwhiteboard.ui.ColorWheelView
import com.cruse.openwhiteboard.ui.ExportChoiceDialog
import com.cruse.openwhiteboard.ui.ExportPagesDialog
import com.cruse.openwhiteboard.ui.FileManagerDialog
import com.cruse.openwhiteboard.ui.PageManagerAdapter
import com.cruse.openwhiteboard.ui.WhiteboardSurfaceView
import android.widget.ArrayAdapter
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.Spinner
import com.cruse.openwhiteboard.model.SlideOrientation
import com.cruse.openwhiteboard.model.SlidePreset
import com.cruse.openwhiteboard.model.SlideUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity(), EngineCallbacksInterface, FileManagerDialog.Callbacks {

    companion object {
        private const val TAG = "MainActivity"
    }

    private lateinit var whiteboardSurface: WhiteboardSurfaceView

    // Docks & Flyouts
    private lateinit var flyoutPen: LinearLayout
    private lateinit var flyoutEraser: LinearLayout
    private lateinit var flyoutShapes: LinearLayout
    private lateinit var flyoutBackground: LinearLayout
    private lateinit var modalSettingsOverlay: FrameLayout
    private lateinit var layoutSelectionContextMenu: LinearLayout

    // Background Panel & Sub-items
    private lateinit var btnBgImages: LinearLayout
    private lateinit var btnBgColorGrid: LinearLayout
    private lateinit var layoutBgDateTimeRow: LinearLayout
    private lateinit var switchDateTimeOverlay: SwitchCompat
    private lateinit var layoutDateTimeOverlay: LinearLayout
    private lateinit var tvDateTimeOverlay: TextView
    private lateinit var cardColorGridSubPanel: LinearLayout
    private lateinit var btnIrlenInfo: ImageView
    private lateinit var layoutBgCustomColorRow: LinearLayout
    private lateinit var btnBgColorWheel: ImageView
    private lateinit var btnApplyBgAllPages: Button

    private var currentSelectedBgColor: Int = Color.WHITE
    private var currentSelectedGridType: Int = 0
    private var isColorPickerForBackground: Boolean = false
    private var activeSlidePreset: SlidePreset = SlidePreset.PRESET_16_9

    private val dateTimeHandler = Handler(Looper.getMainLooper())
    private val dateTimeRunnable = object : Runnable {
        override fun run() {
            updateDateTimeText()
            dateTimeHandler.postDelayed(this, 10000)
        }
    }

    private val pickBgImageLauncher = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) {
            Toast.makeText(this, "Background image selected", Toast.LENGTH_SHORT).show()
        }
    }

    private val safOpenDocumentLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) handleSafOpenUri(uri)
    }

    private val safSaveDocumentLauncher = registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) handleSafSaveUri(uri)
    }

    private val safExportPdfLauncher = registerForActivityResult(ActivityResultContracts.CreateDocument("application/pdf")) { uri ->
        if (uri != null) handleSafExportPdfUri(uri)
    }

    // Shapes Flyout Sub-Items
    private lateinit var btnCloseShapes: ImageButton
    private lateinit var btnShapeLine: ImageButton
    private lateinit var btnShapeArrow: ImageButton
    private lateinit var btnShapeRect: ImageButton
    private lateinit var btnShapeCircle: ImageButton
    private lateinit var btnShapeTriangle: ImageButton
    private lateinit var btnShapeRightTriangle: ImageButton
    private lateinit var btnShapeDiamond: ImageButton
    private lateinit var btnShapeStar: ImageButton
    private lateinit var btnShapeHexagon: ImageButton

    private lateinit var btnShapeCube: ImageButton
    private lateinit var btnShapeCuboid: ImageButton
    private lateinit var btnShapeSphere: ImageButton
    private lateinit var btnShapeCylinder: ImageButton
    private lateinit var btnShapeCone: ImageButton
    private lateinit var btnShapeFrustum: ImageButton
    private lateinit var btnShapePyramid: ImageButton
    private lateinit var btnShapePrism: ImageButton

    // Pen Types & Infinite Color Wheel Modal
    private lateinit var flyPenStandard: ImageButton
    private lateinit var flyPenBrush: ImageButton
    private lateinit var flyPenHighlighter: ImageButton
    private lateinit var flyPenStamp: ImageButton
    private lateinit var flyPenLaser: ImageButton
    private lateinit var flyPenShapeStamp: ImageButton
    private lateinit var flyPenGlitter: ImageButton

    private lateinit var flyWidthThin: ImageButton
    private lateinit var flyWidthMedium: ImageButton
    private lateinit var flyWidthThick: ImageButton

    private lateinit var caretBlack: View
    private lateinit var caretWhite: View
    private lateinit var caretRed: View
    private lateinit var caretBlue: View
    private lateinit var caretCustom: View

    private lateinit var btnColorWheelPicker: ImageButton

    private lateinit var modalColorPickerOverlay: FrameLayout
    private lateinit var colorWheelCanvas: ColorWheelView
    private lateinit var viewLiveColorPreview: View
    private lateinit var etHexInput: EditText
    private lateinit var seekBarVal: SeekBar
    private lateinit var btnCancelColorPicker: Button
    private lateinit var btnApplyCustomColor: Button

    private var activeCustomColor: Int = Color.BLACK

    // Main Tool Dock Buttons (Includes Undo & Redo)
    private lateinit var btnSelect: ImageButton
    private lateinit var btnPen: ImageButton
    private lateinit var btnEraser: ImageButton
    private lateinit var btnShapes: ImageButton
    private lateinit var btnFolder: ImageButton
    private lateinit var btnUndo: ImageButton
    private lateinit var btnRedo: ImageButton

    // Page Nav & Settings Docks
    private lateinit var dockPageNav: LinearLayout
    private lateinit var btnBackground: ImageButton
    private lateinit var btnPrevPage: ImageButton
    private lateinit var btnNextPage: ImageButton
    private lateinit var btnAddPage: ImageButton
    private lateinit var tvPageInfo: TextView
    private lateinit var btnFitPage: ImageButton
    private lateinit var btnMinimap: ImageButton
    private lateinit var dividerSettings: View
    private lateinit var btnSettings: ImageButton
    private lateinit var btnCloseSettings: Button
    private lateinit var btnModeFixedPage: Button
    private lateinit var btnModeOpenCanvas: Button
    private lateinit var btnSettingImportBg: Button
    private lateinit var btnSettingImportObj: Button
    private var layoutSlideSizeSettings: LinearLayout? = null

    // System File Operations Flyout
    private lateinit var flyoutSystemFileMenu: LinearLayout
    private lateinit var btnCloseSystemFileMenu: ImageButton
    private lateinit var btnSysFileNew: LinearLayout
    private lateinit var btnSysFileOpen: LinearLayout
    private lateinit var btnSysFileSave: LinearLayout
    private lateinit var btnSysFileSaveAs: LinearLayout
    private lateinit var btnSysFileExportPdf: LinearLayout
    private lateinit var btnSysFileShare: LinearLayout

    // Flyout Sub-Items
    private lateinit var cardEraserSizeSlider: LinearLayout
    private lateinit var btnEraserNormal: ImageButton
    private lateinit var btnEraserObject: ImageButton
    private lateinit var btnClearPage: ImageButton
    private lateinit var seekBarEraserSize: SeekBar
    private lateinit var btnLockSelection: ImageButton
    private lateinit var btnColorSelection: FrameLayout
    private lateinit var viewSelectionColorDot: View
    private lateinit var btnCopySelection: ImageButton
    private lateinit var btnPasteSelection: ImageButton
    private lateinit var btnDuplicateSelection: ImageButton
    private lateinit var btnFlipHSelection: ImageButton
    private lateinit var btnFlipVSelection: ImageButton
    private lateinit var btnAlignSelection: ImageButton
    private lateinit var btnDeleteSelected: ImageButton
    private lateinit var dividerLock1: View
    private lateinit var dividerLock2: View
    private lateinit var dividerLock3: View
    private lateinit var dividerLock4: View
    private lateinit var dividerLock5: View
    private lateinit var dividerLock6: View
    private lateinit var layoutAlignDropdown: LinearLayout
    private lateinit var btnAlignBringToFront: LinearLayout
    private lateinit var btnAlignSendBackward: LinearLayout
    private lateinit var btnAlignSendToBack: LinearLayout
    private lateinit var layoutTapPastePopup: LinearLayout
    private var lastTapCanvasX: Float = 0f
    private var lastTapCanvasY: Float = 0f
    private var isColorPickerForSelection: Boolean = false

    // Pen Color Swatches
    private lateinit var colorBlack: View
    private lateinit var colorWhite: View
    private lateinit var colorBlue: View
    private lateinit var colorRed: View
    private lateinit var colorGreen: View
    private lateinit var colorYellow: View

    private var activeTool = ToolType.PEN
    private var isFixedCanvasMode = true
    private var currentFilePath: String? = null // tracks last saved/opened .owb path

    // ─── Import Progress Overlay & Background Streaming Badge ─────────────────
    private lateinit var modalImportProgressOverlay: FrameLayout
    private lateinit var tvImportProgressTitle: TextView
    private lateinit var tvImportProgressFileName: TextView
    private lateinit var tvImportProgressCount: TextView
    private lateinit var tvImportProgressPercent: TextView
    private lateinit var progressBarImport: ProgressBar
    private lateinit var tvImportProgressStatus: TextView
    private lateinit var btnCancelImport: Button
    private var isImportCancelled = false

    private lateinit var layoutStreamLoadingBadge: LinearLayout
    private lateinit var pbStreamSpinner: ProgressBar
    private lateinit var tvStreamBadgeStatus: TextView
    private lateinit var pbStreamLinear: ProgressBar
    private lateinit var tvStreamBadgePercent: TextView
    private lateinit var btnCancelStreamLoading: ImageButton

    private lateinit var layoutCanvasPageLoading: LinearLayout
    private lateinit var pbCanvasPageSpinner: ProgressBar
    private lateinit var tvCanvasPageLoadingTitle: TextView
    private lateinit var tvCanvasPageLoadingSubtitle: TextView
    private val priorityStreamPageIndex = java.util.concurrent.atomic.AtomicInteger(-1)
    private var isStreamingImportActive = false
    private val streamingSlideIndices = java.util.Collections.synchronizedSet(mutableSetOf<Int>())

    // ─── Page Manager Panel Components ────────────────────────────────────────
    private lateinit var flyoutPageManager: LinearLayout
    private lateinit var tvPageManagerTitle: TextView
    private lateinit var tvPageManagerCount: TextView
    private lateinit var rvPages: RecyclerView
    private lateinit var layoutPageSelectionPill: LinearLayout
    private lateinit var tvPageSelectionCount: TextView
    private lateinit var btnClearPageSelection: ImageView
    private lateinit var btnDeleteSelectedPages: ImageButton
    private lateinit var btnSelectAllPages: ImageButton
    private lateinit var dividerBatchActions: View
    private lateinit var btnPinPageManager: ImageButton
    private lateinit var btnExpandPageManager: ImageButton
    private lateinit var btnClosePageManager: ImageButton
    private lateinit var pageAdapter: PageManagerAdapter
    private var isPageManagerPinned = false
    private var isPageManagerExpanded = false

    // ─── Runtime & All-Files Storage Permission Launchers ─────────────────────

    private val requestPermissionsLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
            val allGranted = permissions.entries.all { it.value }
            Log.i(TAG, "Runtime storage permissions result: $permissions (allGranted=$allGranted)")
            checkAndRequestStorageManagerPermission()
        }

    private val manageAllFilesLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                if (Environment.isExternalStorageManager()) {
                    Toast.makeText(this, "Full storage access enabled", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Storage access is needed to browse and open whiteboard files", Toast.LENGTH_LONG).show()
                }
            }
        }

    private fun checkAndRequestAllPermissions() {
        val permissionsNeeded = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(android.Manifest.permission.READ_MEDIA_IMAGES) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                permissionsNeeded.add(android.Manifest.permission.READ_MEDIA_IMAGES)
            }
            if (checkSelfPermission(android.Manifest.permission.READ_MEDIA_VIDEO) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                permissionsNeeded.add(android.Manifest.permission.READ_MEDIA_VIDEO)
            }
            if (checkSelfPermission(android.Manifest.permission.READ_MEDIA_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                permissionsNeeded.add(android.Manifest.permission.READ_MEDIA_AUDIO)
            }
        } else {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                permissionsNeeded.add(android.Manifest.permission.READ_EXTERNAL_STORAGE)
            }
            if (checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                permissionsNeeded.add(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
            }
        }

        if (permissionsNeeded.isNotEmpty()) {
            requestPermissionsLauncher.launch(permissionsNeeded.toTypedArray())
        } else {
            checkAndRequestStorageManagerPermission()
        }
    }

    private fun checkAndRequestStorageManagerPermission() {
        showStoragePermissionDialog("browse, open, and save whiteboard notes, slides, and images across internal, SD card, and USB storage")
    }

    private fun showStoragePermissionDialog(actionDescription: String = "access files") {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                androidx.appcompat.app.AlertDialog.Builder(this)
                    .setTitle("Storage Permission Required")
                    .setMessage("OpenWhiteBoard needs full storage access to $actionDescription.\n\nPlease tap 'Allow Access' and enable 'All files access' in settings.")
                    .setPositiveButton("Allow Access") { _, _ ->
                        try {
                            val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
                                data = Uri.parse("package:$packageName")
                            }
                            manageAllFilesLauncher.launch(intent)
                        } catch (e: Throwable) {
                            try {
                                val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                                manageAllFilesLauncher.launch(intent)
                            } catch (e2: Throwable) {
                                Log.e(TAG, "Cannot launch storage settings", e2)
                                Toast.makeText(this, "Please enable storage access in Android Settings", Toast.LENGTH_LONG).show()
                            }
                        }
                    }
                    .setNegativeButton("Later", null)
                    .show()
                return
            }
        } else {
            val permissionsNeeded = mutableListOf<String>()
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                permissionsNeeded.add(android.Manifest.permission.READ_EXTERNAL_STORAGE)
            }
            if (checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                permissionsNeeded.add(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
            }
            if (permissionsNeeded.isNotEmpty()) {
                requestPermissionsLauncher.launch(permissionsNeeded.toTypedArray())
            }
        }
    }

    private fun updateDateTimeText() {
        try {
            val sdf = SimpleDateFormat("EEEE, MMMM d, yyyy  •  hh:mm a", Locale.getDefault())
            tvDateTimeOverlay.text = sdf.format(Date())
        } catch (e: Throwable) {
            Log.e(TAG, "Error formatting date time", e)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        dateTimeHandler.removeCallbacks(dateTimeRunnable)
    }

    // ─── FileManagerDialog.Callbacks ─────────────────────────────────────────

    override fun onFmNewFile() {
        isImportCancelled = true
        isStreamingImportActive = false
        streamingSlideIndices.clear()
        layoutCanvasPageLoading.visibility = View.GONE
        layoutStreamLoadingBadge.visibility = View.GONE
        val (defW, defH) = getDefaultSlideDimensions()
        whiteboardSurface.resetDocumentPages(1, defW, defH)
        currentFilePath = null
        updatePageInfo()
        pageAdapter.notifyDataSetChanged()
        pageAdapter.invalidateThumbnails()
        Toast.makeText(this, "New whiteboard created", Toast.LENGTH_SHORT).show()
    }

    override fun onFmOpenFile(path: String) {
        val file = java.io.File(path)
        if (!file.exists()) {
            Toast.makeText(this, "File not found: ${file.name}", Toast.LENGTH_SHORT).show()
            showStoragePermissionDialog("access storage files")
            return
        }
        whiteboardSurface.runWhenSurfaceReady {
            if (com.cruse.openwhiteboard.ui.FileUtils.isImageFile(file)) {
                val ok = whiteboardSurface.importImage(path)
                if (ok) {
                    Toast.makeText(this, "Imported image: ${file.name}", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Failed to import image", Toast.LENGTH_SHORT).show()
                }
            } else if (com.cruse.openwhiteboard.ui.FileUtils.isSlideFile(file)) {
                val fmPrefs = getSharedPreferences(FileManagerDialog.PREFS_NAME, Context.MODE_PRIVATE)
                val defaultMode = fmPrefs.getString(FileManagerDialog.PREF_DEFAULT_IMPORT_MODE, "background") ?: "background"
                onFmImportSlides(path, defaultMode == "background")
            } else {
                val ok = whiteboardSurface.openDocument(path)
                if (ok) {
                    currentFilePath = path
                    updatePageInfo()
                    pageAdapter.notifyDataSetChanged()
                    pageAdapter.invalidateThumbnails()
                    Toast.makeText(this, "Opened: ${file.name}", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Failed to open document: ${file.name}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun checkAndShowPageLoadingIndicator(pageIndex: Int) {
        if (!isStreamingImportActive || !streamingSlideIndices.contains(pageIndex)) {
            layoutCanvasPageLoading.visibility = View.GONE
            return
        }
        val isLoaded = whiteboardSurface.isPageTextureLoaded(pageIndex)
        if (isLoaded) {
            layoutCanvasPageLoading.visibility = View.GONE
        } else {
            val hasFile = whiteboardSurface.hasPageSlideFile(pageIndex)
            if (hasFile) {
                val ok = whiteboardSurface.ensurePageTextureLoaded(pageIndex)
                if (ok) {
                    layoutCanvasPageLoading.visibility = View.GONE
                    return
                }
            }
            // Page is still being prepared or rendered in background! Show rotating buffering wheel
            tvCanvasPageLoadingTitle.text = "Loading Page ${pageIndex + 1}..."
            tvCanvasPageLoadingSubtitle.text = "Rendering high quality slide"
            layoutCanvasPageLoading.visibility = View.VISIBLE
            // Set priority so background renderer processes this page next!
            priorityStreamPageIndex.set(pageIndex)
        }
    }

    override fun onFmImportSlides(path: String, importAsBackground: Boolean) {
        val sourceFile = File(path)
        if (!sourceFile.exists() || !sourceFile.canRead()) {
            Toast.makeText(this, "Cannot read file: ${sourceFile.name}", Toast.LENGTH_SHORT).show()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
                showStoragePermissionDialog("import slides from storage")
            }
            return
        }

        isImportCancelled = false
        isStreamingImportActive = true
        streamingSlideIndices.clear()
        btnCancelStreamLoading.isEnabled = true

        // Ensure blocking modal is never shown
        modalImportProgressOverlay.visibility = View.GONE

        // Show centered rotating buffering loader immediately on canvas
        tvCanvasPageLoadingTitle.text = "Loading Page 1..."
        tvCanvasPageLoadingSubtitle.text = "Rendering high quality slide"
        layoutCanvasPageLoading.visibility = View.VISIBLE

        lifecycleScope.launch(Dispatchers.IO) {
            var pfd: ParcelFileDescriptor? = null
            var renderer: PdfRenderer? = null
            var localFile = sourceFile
            var reusableBitmap: Bitmap? = null
            try {
                // Always copy to cacheDir to guarantee a fresh seekable file descriptor
                val safeExt = if (sourceFile.name.lowercase().endsWith(".pdf")) ".pdf" else ".dat"
                val temp = File(cacheDir, "slides_import_${System.currentTimeMillis()}$safeExt")
                sourceFile.inputStream().buffered().use { input ->
                    temp.outputStream().buffered().use { output ->
                        input.copyTo(output)
                    }
                }
                if (!temp.exists() || temp.length() == 0L) {
                    withContext(Dispatchers.Main) {
                        isStreamingImportActive = false
                        streamingSlideIndices.clear()
                        layoutCanvasPageLoading.visibility = View.GONE
                        layoutStreamLoadingBadge.visibility = View.GONE
                        Toast.makeText(this@MainActivity, "Could not read file — check storage access", Toast.LENGTH_LONG).show()
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
                            showStoragePermissionDialog("import slides from storage")
                        }
                    }
                    return@launch
                }
                localFile = temp

                pfd = ParcelFileDescriptor.open(localFile, ParcelFileDescriptor.MODE_READ_ONLY)
                renderer = PdfRenderer(pfd)
                val pageCount = renderer.pageCount

                if (pageCount <= 0) {
                    withContext(Dispatchers.Main) {
                        isStreamingImportActive = false
                        streamingSlideIndices.clear()
                        layoutCanvasPageLoading.visibility = View.GONE
                        layoutStreamLoadingBadge.visibility = View.GONE
                        Toast.makeText(this@MainActivity, "Document has 0 pages", Toast.LENGTH_SHORT).show()
                    }
                    return@launch
                }

                val (defW, defH) = getDefaultSlideDimensions()
                val baseCanvasW = defW
                val baseCanvasH = defH

                var startPageIndex = 0
                val isEmptyDoc = whiteboardSurface.isDocumentEmpty()
                val curActive = whiteboardSurface.getActivePageIndex()

                withContext(Dispatchers.Main) {
                    if (isEmptyDoc) {
                        startPageIndex = 0
                        whiteboardSurface.resetDocumentPages(pageCount, baseCanvasW, baseCanvasH)
                    } else {
                        startPageIndex = curActive + 1
                        whiteboardSurface.insertPages(startPageIndex, pageCount, baseCanvasW, baseCanvasH)
                    }
                    for (k in 0 until pageCount) {
                        streamingSlideIndices.add(startPageIndex + k)
                    }
                    updatePageInfo()
                    pageAdapter.notifyDataSetChanged()
                }

                val docDir = File(filesDir, "board_docs/doc_${System.currentTimeMillis()}").apply { mkdirs() }
                val targetLongEdge = 2560f

                // ── STEP 1: RENDER PAGE 0 IMMEDIATELY & GIVE INSTANT USER CONTROL ────
                val firstPdfPage = renderer.openPage(0)
                val firstPdfW = firstPdfPage.width.takeIf { it > 0 } ?: 1920
                val firstPdfH = firstPdfPage.height.takeIf { it > 0 } ?: 1080
                val firstScale = (targetLongEdge / maxOf(firstPdfW, firstPdfH)).coerceIn(1.5f, 3.5f)
                val firstRenderW = (firstPdfW * firstScale).toInt().coerceAtLeast(1)
                val firstRenderH = (firstPdfH * firstScale).toInt().coerceAtLeast(1)

                reusableBitmap = Bitmap.createBitmap(firstRenderW, firstRenderH, Bitmap.Config.ARGB_8888)
                val firstCanvas = Canvas(reusableBitmap!!)
                firstCanvas.drawColor(Color.WHITE)
                firstPdfPage.render(reusableBitmap!!, null, null, PdfRenderer.Page.RENDER_MODE_FOR_PRINT)
                firstPdfPage.close()

                val firstSlideFile = File(docDir, "slide_${System.currentTimeMillis()}_0.png")
                firstSlideFile.outputStream().buffered().use { out ->
                    reusableBitmap?.compress(Bitmap.CompressFormat.PNG, 100, out)
                }

                val firstCanvasPageW = if (firstPdfW >= firstPdfH) baseCanvasW else ((baseCanvasH * firstPdfW) / firstPdfH)
                val firstCanvasPageH = if (firstPdfW >= firstPdfH) ((baseCanvasW * firstPdfH) / firstPdfW) else baseCanvasH

                withContext(Dispatchers.Main) {
                    val targetIndex = startPageIndex
                    streamingSlideIndices.remove(targetIndex)
                    whiteboardSurface.setPageDimensions(targetIndex, firstCanvasPageW, firstCanvasPageH)
                    whiteboardSurface.setPageSlidePath(targetIndex, firstSlideFile.absolutePath)
                    val textureId = whiteboardSurface.uploadTexture(reusableBitmap!!)
                    if (importAsBackground) {
                        whiteboardSurface.setPageBackgroundTexture(targetIndex, textureId, firstSlideFile.absolutePath)
                    } else {
                        whiteboardSurface.addImageToPage(targetIndex, firstSlideFile.absolutePath, textureId, 0f, 0f, firstCanvasPageW, firstCanvasPageH)
                    }
                    whiteboardSurface.setActivePage(targetIndex)
                    updatePageInfo()
                    pageAdapter.notifyDataSetChanged()
                    pageAdapter.invalidateThumbnails()

                    // Hide buffering indicator for page 0 once loaded
                    if (whiteboardSurface.getActivePageIndex() == targetIndex) {
                        layoutCanvasPageLoading.visibility = View.GONE
                    }

                    if (pageCount > 1) {
                        // SHOW SLEEK FLOATING BACKGROUND STREAMING BADGE
                        pbStreamLinear.max = pageCount
                        pbStreamLinear.progress = 1
                        val percent = (100 / pageCount)
                        tvStreamBadgePercent.text = "$percent%"
                        tvStreamBadgeStatus.text = "Loading slide 2 of $pageCount in background..."
                        btnCancelStreamLoading.isEnabled = true
                        layoutStreamLoadingBadge.visibility = View.VISIBLE
                    } else {
                        isStreamingImportActive = false
                        streamingSlideIndices.clear()
                        layoutStreamLoadingBadge.visibility = View.GONE
                        val modeLabel = if (importAsBackground) "Background" else "Object"
                        Toast.makeText(this@MainActivity, "Imported 1 slide as $modeLabel", Toast.LENGTH_SHORT).show()
                    }
                }

                // ── STEP 2: STREAM REMAINING PAGES IN BACKGROUND WITH PRIORITY QUEUE ──
                val remainingIndices = java.util.Collections.synchronizedList((1 until pageCount).toMutableList())

                while (remainingIndices.isNotEmpty() && !isImportCancelled) {
                    val prio = priorityStreamPageIndex.getAndSet(-1)
                    val targetRelIndex = if (prio >= startPageIndex && (prio - startPageIndex) in remainingIndices) {
                        val rel = prio - startPageIndex
                        remainingIndices.remove(rel)
                        rel
                    } else {
                        remainingIndices.removeAt(0)
                    }

                    val targetIndex = startPageIndex + targetRelIndex
                    val pdfPage = renderer.openPage(targetRelIndex)
                    val pdfW = pdfPage.width.takeIf { it > 0 } ?: 1920
                    val pdfH = pdfPage.height.takeIf { it > 0 } ?: 1080

                    val scale = (targetLongEdge / maxOf(pdfW, pdfH)).coerceIn(1.5f, 3.5f)
                    val renderW = (pdfW * scale).toInt().coerceAtLeast(1)
                    val renderH = (pdfH * scale).toInt().coerceAtLeast(1)

                    if (reusableBitmap == null || reusableBitmap?.width != renderW || reusableBitmap?.height != renderH || reusableBitmap?.isRecycled == true) {
                        reusableBitmap?.recycle()
                        reusableBitmap = Bitmap.createBitmap(renderW, renderH, Bitmap.Config.ARGB_8888)
                    }

                    val bitmap = reusableBitmap!!
                    try {
                        val canvas = Canvas(bitmap)
                        canvas.drawColor(Color.WHITE)
                        pdfPage.render(bitmap, null, null, PdfRenderer.Page.RENDER_MODE_FOR_PRINT)
                    } finally {
                        pdfPage.close()
                    }

                    val slideFile = File(docDir, "slide_${System.currentTimeMillis()}_${targetRelIndex}.png")
                    slideFile.outputStream().buffered().use { out ->
                        bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)
                    }

                    val canvasPageW = if (pdfW >= pdfH) baseCanvasW else ((baseCanvasH * pdfW) / pdfH)
                    val canvasPageH = if (pdfW >= pdfH) ((baseCanvasW * pdfH) / pdfW) else baseCanvasH

                    withContext(Dispatchers.Main) {
                        try {
                            streamingSlideIndices.remove(targetIndex)
                            whiteboardSurface.setPageDimensions(targetIndex, canvasPageW, canvasPageH)
                            whiteboardSurface.setPageSlidePath(targetIndex, slideFile.absolutePath)
                            if (importAsBackground) {
                                whiteboardSurface.setPageBackgroundTexture(targetIndex, 0, slideFile.absolutePath)
                            }

                            // If user is currently looking at this page, upload texture immediately and hide buffering spinner!
                            if (whiteboardSurface.getActivePageIndex() == targetIndex) {
                                whiteboardSurface.ensurePageTextureLoaded(targetIndex)
                                layoutCanvasPageLoading.visibility = View.GONE
                            }

                            // Update background streaming badge
                            val doneCount = pageCount - remainingIndices.size
                            pbStreamLinear.progress = doneCount
                            val percent = (doneCount * 100) / pageCount
                            tvStreamBadgePercent.text = "$percent%"
                            if (remainingIndices.isNotEmpty()) {
                                tvStreamBadgeStatus.text = "Loading slide ${doneCount + 1} of $pageCount in background..."
                            } else {
                                tvStreamBadgeStatus.text = "Finishing background import..."
                            }
                            pageAdapter.invalidateThumbnail(targetIndex)
                        } catch (t: Throwable) {
                            Log.e(TAG, "Error setting slide page data for index $targetIndex", t)
                        }
                    }
                }

                withContext(Dispatchers.Main) {
                    isStreamingImportActive = false
                    streamingSlideIndices.clear()
                    layoutStreamLoadingBadge.visibility = View.GONE
                    layoutCanvasPageLoading.visibility = View.GONE

                    if (isImportCancelled) {
                        Toast.makeText(this@MainActivity, "Background import stopped", Toast.LENGTH_SHORT).show()
                    } else {
                        updatePageInfo()
                        pageAdapter.notifyDataSetChanged()
                        pageAdapter.invalidateThumbnails()
                        currentFilePath = null
                        val totalPages = whiteboardSurface.getPageCount()
                        Toast.makeText(
                            this@MainActivity,
                            "All $pageCount slides loaded in high quality (Total: $totalPages pages)",
                            Toast.LENGTH_SHORT
                        ).show()
                    }
                }
            } catch (e: SecurityException) {
                Log.e(TAG, "SecurityException importing slides", e)
                withContext(Dispatchers.Main) {
                    isStreamingImportActive = false
                    streamingSlideIndices.clear()
                    layoutStreamLoadingBadge.visibility = View.GONE
                    layoutCanvasPageLoading.visibility = View.GONE
                    Toast.makeText(this@MainActivity, "Permission denied — cannot open this file", Toast.LENGTH_LONG).show()
                    showStoragePermissionDialog("open and import PDF slides")
                }
            } catch (e: OutOfMemoryError) {
                Log.e(TAG, "OOM importing slides", e)
                withContext(Dispatchers.Main) {
                    isStreamingImportActive = false
                    streamingSlideIndices.clear()
                    layoutStreamLoadingBadge.visibility = View.GONE
                    layoutCanvasPageLoading.visibility = View.GONE
                    Toast.makeText(this@MainActivity, "Not enough memory. Try a smaller document", Toast.LENGTH_LONG).show()
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Error importing slides: ${e.javaClass.simpleName}: ${e.message}", e)
                withContext(Dispatchers.Main) {
                    isStreamingImportActive = false
                    streamingSlideIndices.clear()
                    layoutStreamLoadingBadge.visibility = View.GONE
                    layoutCanvasPageLoading.visibility = View.GONE
                    val msg = when {
                        e.message?.contains("password", ignoreCase = true) == true -> "PDF is password-protected"
                        e.message?.contains("format", ignoreCase = true) == true -> "Unsupported file format"
                        else -> "Failed to import slides: ${e.localizedMessage ?: "Unknown error"}"
                    }
                    Toast.makeText(this@MainActivity, msg, Toast.LENGTH_LONG).show()
                }
            } finally {
                try {
                    renderer?.close()
                    pfd?.close()
                    if (localFile != sourceFile && localFile.exists()) {
                        localFile.delete()
                    }
                } catch (e: Throwable) {
                    Log.e(TAG, "Error closing PdfRenderer resources", e)
                }
            }
        }
    }

    override fun onFmSaveFile(path: String) {
        val ok = whiteboardSurface.saveDocument(path)
        if (ok) {
            currentFilePath = path
            Toast.makeText(this, "Saved: ${path.substringAfterLast('/')}", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Failed to save file", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onFmExportPdf(path: String, pageIndices: List<Int>?) {
        Toast.makeText(this, "Exporting PDF in high resolution...", Toast.LENGTH_SHORT).show()
        lifecycleScope.launch(Dispatchers.IO) {
            val ok = whiteboardSurface.exportPdf(path, pageIndices)
            withContext(Dispatchers.Main) {
                if (ok) {
                    val countStr = if (pageIndices != null) "${pageIndices.size} pages" else "document"
                    Toast.makeText(this@MainActivity, "PDF exported ($countStr): ${path.substringAfterLast('/')}", Toast.LENGTH_LONG).show()
                } else {
                    Toast.makeText(this@MainActivity, "PDF export failed", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    override fun onFmSharePages(pageIndices: List<Int>) {
        sharePdfFlow(pageIndices)
    }

    override fun getWhiteboardSurface(): WhiteboardSurfaceView = whiteboardSurface

    // ─── Android Storage Access Framework (SAF) System File Handlers ─────────

    private fun getUriFileName(uri: Uri): String {
        var name: String? = null
        try {
            val cursor = contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
            cursor?.use {
                if (it.moveToFirst()) {
                    val idx = it.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) {
                        name = it.getString(idx)
                    }
                }
            }
        } catch (e: Throwable) {
            Log.e(TAG, "Error querying display name", e)
        }
        if (name.isNullOrBlank()) {
            name = uri.lastPathSegment?.substringAfterLast('/')?.substringAfterLast(':')
        }
        return if (!name.isNullOrBlank()) name!! else "document"
    }

    private fun handleSafOpenUri(uri: Uri) {
        val rawName = getUriFileName(uri)
        val safeName = rawName.replace("[^a-zA-Z0-9._-]".toRegex(), "_").ifBlank { "document" }
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val tempFile = File(cacheDir, "saf_open_${System.currentTimeMillis()}_${safeName}")
                val inputStream = try {
                    contentResolver.openInputStream(uri)
                } catch (se: SecurityException) {
                    Log.e(TAG, "SecurityException opening SAF URI", se)
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "Permission denied — cannot access this file", Toast.LENGTH_LONG).show()
                        showStoragePermissionDialog("open files")
                    }
                    return@launch
                }
                if (inputStream == null) {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "Could not read file from Android file manager", Toast.LENGTH_SHORT).show()
                    }
                    return@launch
                }
                inputStream.buffered().use { input ->
                    tempFile.outputStream().buffered().use { output ->
                        input.copyTo(output)
                    }
                }
                if (!tempFile.exists() || tempFile.length() == 0L) {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "File is empty or could not be read", Toast.LENGTH_SHORT).show()
                    }
                    return@launch
                }
                withContext(Dispatchers.Main) {
                    onFmOpenFile(tempFile.absolutePath)
                }
            } catch (e: SecurityException) {
                Log.e(TAG, "SecurityException in handleSafOpenUri", e)
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@MainActivity, "Permission denied — cannot access this file", Toast.LENGTH_LONG).show()
                    showStoragePermissionDialog("open files")
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Error opening SAF uri", e)
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@MainActivity, "Error opening file: ${e.localizedMessage ?: e.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun handleSafSaveUri(uri: Uri) {
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val tempFile = File(cacheDir, "saf_save_${System.currentTimeMillis()}.owb")
                val ok = whiteboardSurface.saveDocument(tempFile.absolutePath)
                if (ok && tempFile.exists()) {
                    contentResolver.openOutputStream(uri)?.use { output ->
                        tempFile.inputStream().use { input ->
                            input.copyTo(output)
                        }
                    }
                    tempFile.delete()
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "Document saved successfully", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "Failed to save document", Toast.LENGTH_SHORT).show()
                    }
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Error saving SAF uri", e)
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@MainActivity, "Error saving file: ${e.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private var pendingExportPageIndices: List<Int>? = null

    private fun handleSafExportPdfUri(uri: Uri) {
        val selectedPages = pendingExportPageIndices
        pendingExportPageIndices = null
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val tempFile = File(cacheDir, "saf_export_${System.currentTimeMillis()}.pdf")
                val ok = whiteboardSurface.exportPdf(tempFile.absolutePath, selectedPages)
                if (ok && tempFile.exists() && tempFile.length() > 0L) {
                    contentResolver.openOutputStream(uri)?.use { output ->
                        tempFile.inputStream().use { input ->
                            input.copyTo(output)
                        }
                    }
                    val countStr = if (selectedPages != null) "${selectedPages.size} pages" else "document"
                    tempFile.delete()
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "PDF exported successfully ($countStr)", Toast.LENGTH_LONG).show()
                    }
                } else {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "Failed to export PDF", Toast.LENGTH_SHORT).show()
                    }
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Error exporting PDF SAF uri", e)
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@MainActivity, "Error exporting PDF: ${e.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun promptPdfExportFlow(onProceed: (selectedIndices: List<Int>) -> Unit) {
        val totalCount = whiteboardSurface.getPageCount()
        val choiceDialog = ExportChoiceDialog.newInstance(totalCount, object : ExportChoiceDialog.Callbacks {
            override fun onWholeDocumentSelected() {
                onProceed((0 until totalCount).toList())
            }

            override fun onCustomPagesSelected() {
                val pagesDialog = ExportPagesDialog.newInstance(whiteboardSurface, object : ExportPagesDialog.Callbacks {
                    override fun onExportPagesConfirmed(selectedPageIndices: List<Int>) {
                        onProceed(selectedPageIndices)
                    }
                })
                pagesDialog.show(supportFragmentManager, "ExportPagesDialog")
            }
        })
        choiceDialog.show(supportFragmentManager, "ExportChoiceDialog")
    }

    private fun promptPdfShareFlow() {
        val totalCount = whiteboardSurface.getPageCount()
        val choiceDialog = ExportChoiceDialog.newInstance(totalCount, isShareMode = true, object : ExportChoiceDialog.Callbacks {
            override fun onWholeDocumentSelected() {
                onFmSharePages((0 until totalCount).toList())
            }

            override fun onCustomPagesSelected() {
                val pagesDialog = ExportPagesDialog.newInstance(whiteboardSurface, isShareMode = true, object : ExportPagesDialog.Callbacks {
                    override fun onExportPagesConfirmed(selectedPageIndices: List<Int>) {
                        onFmSharePages(selectedPageIndices)
                    }
                })
                pagesDialog.show(supportFragmentManager, "ExportPagesDialog")
            }
        })
        choiceDialog.show(supportFragmentManager, "ExportChoiceDialog")
    }

    private fun sharePdfFlow(selectedPageIndices: List<Int>) {
        Toast.makeText(this, "Preparing high-resolution PDF for sharing...", Toast.LENGTH_SHORT).show()
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                // Clean up old temporary shared files in cacheDir
                cacheDir.listFiles { _, name -> name.startsWith("shared_whiteboard_") && name.endsWith(".pdf") }
                    ?.forEach { try { it.delete() } catch (t: Throwable) {} }

                val tempFile = File(cacheDir, "shared_whiteboard_${System.currentTimeMillis()}.pdf")
                val ok = whiteboardSurface.exportPdf(tempFile.absolutePath, selectedPageIndices)
                if (ok && tempFile.exists() && tempFile.length() > 0L) {
                    val contentUri: Uri = FileProvider.getUriForFile(
                        this@MainActivity,
                        "${applicationContext.packageName}.fileprovider",
                        tempFile
                    )
                    withContext(Dispatchers.Main) {
                        val shareIntent = Intent(Intent.ACTION_SEND).apply {
                            type = "application/pdf"
                            putExtra(Intent.EXTRA_STREAM, contentUri)
                            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                        }
                        val chooser = Intent.createChooser(shareIntent, "Share Whiteboard PDF via")
                        startActivity(chooser)
                    }
                } else {
                    withContext(Dispatchers.Main) {
                        Toast.makeText(this@MainActivity, "Failed to render PDF for sharing", Toast.LENGTH_SHORT).show()
                    }
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Error sharing PDF", e)
                withContext(Dispatchers.Main) {
                    Toast.makeText(this@MainActivity, "Error sharing PDF: ${e.localizedMessage ?: e.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        try {
            supportRequestWindowFeature(Window.FEATURE_NO_TITLE)
            enableImmersiveMode()
            setContentView(R.layout.activity_main)

            checkAndRequestAllPermissions()

            window.decorView.post {
                enableImmersiveMode()
            }

            @Suppress("DEPRECATION")
            window.decorView.setOnSystemUiVisibilityChangeListener { visibility ->
                if ((visibility and View.SYSTEM_UI_FLAG_FULLSCREEN) == 0 ||
                    (visibility and View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0) {
                    enableImmersiveMode()
                }
            }

            (window.decorView as? ViewGroup)?.let {
                it.clipChildren = false
                it.clipToPadding = false
            }
            (findViewById<ViewGroup>(android.R.id.content))?.let {
                it.clipChildren = false
                it.clipToPadding = false
            }

            whiteboardSurface = findViewById(R.id.whiteboardSurface)
            whiteboardSurface.engineCallbacks = this
            whiteboardSurface.attachToLifecycle(this)
            whiteboardSurface.initEngine(filesDir.absolutePath)
            whiteboardSurface.onCanvasTouchListener = {
                runOnUiThread {
                    closeFlyouts()
                    layoutTapPastePopup.visibility = View.GONE
                }
            }
            whiteboardSurface.onCanvasTapListener = { screenX, screenY ->
                runOnUiThread {
                    // Tap on canvas while in selection tool → attempt to select object under tap
                    if (activeTool == ToolType.SELECTION) {
                        layoutTapPastePopup.visibility = View.GONE
                        // tapSelectAt converts screen→canvas internally and fires onSelectionChanged
                        whiteboardSurface.tapSelectAt(screenX, screenY)
                    }
                }
            }

            whiteboardSurface.onCanvasLongPressListener = { screenX, screenY ->
                runOnUiThread {
                    // Long-press on canvas while in selection tool (and clipboard has content) → show paste popup
                    if (activeTool == ToolType.SELECTION && whiteboardSurface.hasClipboard() &&
                        layoutSelectionContextMenu.visibility != View.VISIBLE) {
                        val pt = whiteboardSurface.screenToCanvas(screenX, screenY)
                        lastTapCanvasX = pt.x
                        lastTapCanvasY = pt.y

                        // Position the popup near the long-press point
                        val density = resources.displayMetrics.density
                        val popW = if (layoutTapPastePopup.width > 0) layoutTapPastePopup.width.toFloat() else (110f * density)
                        val popH = if (layoutTapPastePopup.height > 0) layoutTapPastePopup.height.toFloat() else (44f * density)

                        val targetX = (screenX - popW / 2f).coerceIn(16f, (whiteboardSurface.width - popW - 16f).coerceAtLeast(16f))
                        var targetY = screenY - popH - 16f
                        if (targetY < 16f) targetY = screenY + 16f
                        val maxBottomY = (whiteboardSurface.height - popH - 110f).coerceAtLeast(16f)
                        targetY = targetY.coerceIn(16f, maxBottomY)

                        layoutTapPastePopup.x = targetX
                        layoutTapPastePopup.y = targetY
                        layoutTapPastePopup.visibility = View.VISIBLE
                    }
                }
            }

            bindUi()
            handleIntent(intent)
        } catch (e: Throwable) {
            Log.e(TAG, "Fatal error in MainActivity.onCreate", e)
        }
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        val savePath = intent?.getStringExtra("save_file_path")?.trim('\'', '"')
        val filePath = intent?.getStringExtra("open_file_path")?.trim('\'', '"')
        val uri = intent?.data
        if (!savePath.isNullOrBlank()) {
            whiteboardSurface.runWhenSurfaceReady {
                onFmSaveFile(savePath)
            }
        } else if (!filePath.isNullOrBlank()) {
            whiteboardSurface.runWhenSurfaceReady {
                onFmOpenFile(filePath)
            }
        } else if (uri != null) {
            whiteboardSurface.runWhenSurfaceReady {
                handleSafOpenUri(uri)
            }
        }
    }

    private fun bindUi() {
        flyoutPen            = findViewById(R.id.flyoutPen)
        flyoutEraser         = findViewById(R.id.flyoutEraser)
        flyoutBackground     = findViewById(R.id.flyoutBackground)
        modalSettingsOverlay = findViewById(R.id.modalSettingsOverlay)

        flyoutSystemFileMenu     = findViewById(R.id.flyoutSystemFileMenu)
        btnCloseSystemFileMenu   = findViewById(R.id.btnCloseSystemFileMenu)
        btnSysFileNew            = findViewById(R.id.btnSysFileNew)
        btnSysFileOpen           = findViewById(R.id.btnSysFileOpen)
        btnSysFileSave           = findViewById(R.id.btnSysFileSave)
        btnSysFileSaveAs         = findViewById(R.id.btnSysFileSaveAs)
        btnSysFileExportPdf      = findViewById(R.id.btnSysFileExportPdf)
        btnSysFileShare          = findViewById(R.id.btnSysFileShare)

        btnSelect   = findViewById(R.id.btnSelect)
        btnPen      = findViewById(R.id.btnPen)
        btnEraser   = findViewById(R.id.btnEraser)
        btnShapes   = findViewById(R.id.btnShapes)
        btnFolder   = findViewById(R.id.btnFolder)
        btnUndo     = findViewById(R.id.btnUndo)
        btnRedo     = findViewById(R.id.btnRedo)

        dockPageNav      = findViewById(R.id.dockPageNav)
        btnBackground    = findViewById(R.id.btnBackground)
        btnPrevPage      = findViewById(R.id.btnPrevPage)
        btnNextPage      = findViewById(R.id.btnNextPage)
        btnAddPage       = findViewById(R.id.btnAddPage)
        tvPageInfo       = findViewById(R.id.tvPageInfo)
        btnFitPage       = findViewById(R.id.btnFitPage)
        btnMinimap       = findViewById(R.id.btnMinimap)
        dividerSettings  = findViewById(R.id.dividerSettings)
        btnSettings      = findViewById(R.id.btnSettings)
        btnCloseSettings = findViewById(R.id.btnCloseSettings)
        btnModeFixedPage = findViewById(R.id.btnModeFixedPage)
        btnModeOpenCanvas = findViewById(R.id.btnModeOpenCanvas)
        btnSettingImportBg  = findViewById(R.id.btnSettingImportBg)
        btnSettingImportObj = findViewById(R.id.btnSettingImportObj)
        layoutSlideSizeSettings = findViewById(R.id.layoutSlideSizeSettings)

        flyPenStandard       = findViewById(R.id.flyPenStandard)
        flyPenBrush          = findViewById(R.id.flyPenBrush)
        flyPenHighlighter    = findViewById(R.id.flyPenHighlighter)
        flyPenStamp          = findViewById(R.id.flyPenStamp)
        flyPenLaser          = findViewById(R.id.flyPenLaser)
        flyPenShapeStamp     = findViewById(R.id.flyPenShapeStamp)
        flyPenGlitter        = findViewById(R.id.flyPenGlitter)

        flyWidthThin         = findViewById(R.id.flyWidthThin)
        flyWidthMedium       = findViewById(R.id.flyWidthMedium)
        flyWidthThick        = findViewById(R.id.flyWidthThick)

        caretBlack           = findViewById(R.id.caretBlack)
        caretWhite           = findViewById(R.id.caretWhite)
        caretRed             = findViewById(R.id.caretRed)
        caretBlue            = findViewById(R.id.caretBlue)
        caretCustom          = findViewById(R.id.caretCustom)

        btnColorWheelPicker  = findViewById(R.id.btnColorWheelPicker)

        modalColorPickerOverlay = findViewById(R.id.modalColorPickerOverlay)
        colorWheelCanvas        = findViewById(R.id.colorWheelCanvas)
        viewLiveColorPreview    = findViewById(R.id.viewLiveColorPreview)
        etHexInput              = findViewById(R.id.etHexInput)
        seekBarVal              = findViewById(R.id.seekBarVal)
        btnCancelColorPicker    = findViewById(R.id.btnCancelColorPicker)
        btnApplyCustomColor     = findViewById(R.id.btnApplyCustomColor)

        // Document / Slides Import Progress Overlay & Floating Streaming Badge
        modalImportProgressOverlay = findViewById(R.id.modalImportProgressOverlay)
        tvImportProgressTitle      = findViewById(R.id.tvImportProgressTitle)
        tvImportProgressFileName   = findViewById(R.id.tvImportProgressFileName)
        tvImportProgressCount      = findViewById(R.id.tvImportProgressCount)
        tvImportProgressPercent    = findViewById(R.id.tvImportProgressPercent)
        progressBarImport          = findViewById(R.id.progressBarImport)
        tvImportProgressStatus     = findViewById(R.id.tvImportProgressStatus)
        btnCancelImport            = findViewById(R.id.btnCancelImport)

        layoutStreamLoadingBadge = findViewById(R.id.layoutStreamLoadingBadge)
        pbStreamSpinner          = findViewById(R.id.pbStreamSpinner)
        tvStreamBadgeStatus      = findViewById(R.id.tvStreamBadgeStatus)
        pbStreamLinear           = findViewById(R.id.pbStreamLinear)
        tvStreamBadgePercent     = findViewById(R.id.tvStreamBadgePercent)
        btnCancelStreamLoading   = findViewById(R.id.btnCancelStreamLoading)

        layoutCanvasPageLoading     = findViewById(R.id.layoutCanvasPageLoading)
        pbCanvasPageSpinner         = findViewById(R.id.pbCanvasPageSpinner)
        tvCanvasPageLoadingTitle    = findViewById(R.id.tvCanvasPageLoadingTitle)
        tvCanvasPageLoadingSubtitle = findViewById(R.id.tvCanvasPageLoadingSubtitle)
        layoutCanvasPageLoading.visibility = View.GONE

        btnCancelImport.setOnClickListener {
            isImportCancelled = true
            isStreamingImportActive = false
            streamingSlideIndices.clear()
            layoutCanvasPageLoading.visibility = View.GONE
            tvImportProgressStatus.text = "Cancelling import..."
            btnCancelImport.isEnabled = false
        }

        btnCancelStreamLoading.setOnClickListener {
            isImportCancelled = true
            isStreamingImportActive = false
            streamingSlideIndices.clear()
            layoutCanvasPageLoading.visibility = View.GONE
            tvStreamBadgeStatus.text = "Cancelling background import..."
            btnCancelStreamLoading.isEnabled = false
        }

        cardEraserSizeSlider = findViewById(R.id.cardEraserSizeSlider)
        btnEraserNormal      = findViewById(R.id.btnEraserNormal)
        btnEraserObject      = findViewById(R.id.btnEraserObject)
        btnClearPage         = findViewById(R.id.btnClearPage)
        seekBarEraserSize    = findViewById(R.id.seekBarEraserSize)

        // Shapes Flyout Sub-Items
        flyoutShapes           = findViewById(R.id.flyoutShapes)
        btnCloseShapes         = findViewById(R.id.btnCloseShapes)
        btnShapeLine           = findViewById(R.id.btnShapeLine)
        btnShapeArrow          = findViewById(R.id.btnShapeArrow)
        btnShapeRect           = findViewById(R.id.btnShapeRect)
        btnShapeCircle         = findViewById(R.id.btnShapeCircle)
        btnShapeTriangle       = findViewById(R.id.btnShapeTriangle)
        btnShapeRightTriangle  = findViewById(R.id.btnShapeRightTriangle)
        btnShapeDiamond        = findViewById(R.id.btnShapeDiamond)
        btnShapeStar           = findViewById(R.id.btnShapeStar)
        btnShapeHexagon        = findViewById(R.id.btnShapeHexagon)

        btnShapeCube           = findViewById(R.id.btnShapeCube)
        btnShapeCuboid         = findViewById(R.id.btnShapeCuboid)
        btnShapeSphere         = findViewById(R.id.btnShapeSphere)
        btnShapeCylinder       = findViewById(R.id.btnShapeCylinder)
        btnShapeCone           = findViewById(R.id.btnShapeCone)
        btnShapeFrustum        = findViewById(R.id.btnShapeFrustum)
        btnShapePyramid        = findViewById(R.id.btnShapePyramid)
        btnShapePrism          = findViewById(R.id.btnShapePrism)

        layoutSelectionContextMenu = findViewById(R.id.layoutSelectionContextMenu)
        btnLockSelection           = findViewById(R.id.btnLockSelection)
        btnColorSelection          = findViewById(R.id.btnColorSelection)
        viewSelectionColorDot      = findViewById(R.id.viewSelectionColorDot)
        btnCopySelection           = findViewById(R.id.btnCopySelection)
        btnPasteSelection          = findViewById(R.id.btnPasteSelection)
        btnDuplicateSelection      = findViewById(R.id.btnDuplicateSelection)
        btnFlipHSelection          = findViewById(R.id.btnFlipHSelection)
        btnFlipVSelection          = findViewById(R.id.btnFlipVSelection)
        btnAlignSelection          = findViewById(R.id.btnAlignSelection)
        btnDeleteSelected          = findViewById(R.id.btnDeleteSelected)
        dividerLock1               = findViewById(R.id.dividerLock1)
        dividerLock2               = findViewById(R.id.dividerLock2)
        dividerLock3               = findViewById(R.id.dividerLock3)
        dividerLock4               = findViewById(R.id.dividerLock4)
        dividerLock5               = findViewById(R.id.dividerLock5)
        dividerLock6               = findViewById(R.id.dividerLock6)
        layoutAlignDropdown        = findViewById(R.id.layoutAlignDropdown)
        btnAlignBringToFront       = findViewById(R.id.btnAlignBringToFront)
        btnAlignSendBackward       = findViewById(R.id.btnAlignSendBackward)
        btnAlignSendToBack         = findViewById(R.id.btnAlignSendToBack)
        layoutTapPastePopup        = findViewById(R.id.layoutTapPastePopup)

        fun updatePasteButtonState() {
            val hasClip = whiteboardSurface.hasClipboard()
            btnPasteSelection.isEnabled = hasClip
            btnPasteSelection.alpha = if (hasClip) 1.0f else 0.4f
        }

        btnLockSelection.setOnClickListener {
            whiteboardSurface.lockSelected()
        }

        btnColorSelection.setOnClickListener {
            isColorPickerForSelection = true
            val curColor = whiteboardSurface.getSelectedColor()
            activeCustomColor = curColor
            colorWheelCanvas.setColor(curColor)
            viewLiveColorPreview.backgroundTintList = ColorStateList.valueOf(curColor)
            etHexInput.setText(String.format("#%06X", 0xFFFFFF and curColor))
            modalColorPickerOverlay.visibility = View.VISIBLE
        }

        btnCopySelection.setOnClickListener {
            whiteboardSurface.copySelected()
            updatePasteButtonState()
        }

        btnPasteSelection.setOnClickListener {
            whiteboardSurface.paste()
            updatePasteButtonState()
        }

        layoutTapPastePopup.setOnClickListener {
            if (whiteboardSurface.hasClipboard()) {
                whiteboardSurface.pasteAt(lastTapCanvasX, lastTapCanvasY)
                layoutTapPastePopup.visibility = View.GONE
            }
        }

        btnDuplicateSelection.setOnClickListener {
            whiteboardSurface.duplicateSelected()
        }

        btnFlipHSelection.setOnClickListener {
            whiteboardSurface.flipHorizontalSelected()
        }

        btnFlipVSelection.setOnClickListener {
            whiteboardSurface.flipVerticalSelected()
        }

        fun positionAlignDropdown() {
            val density = resources.displayMetrics.density
            layoutAlignDropdown.measure(
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED)
            )
            val dropW = layoutAlignDropdown.measuredWidth.toFloat().takeIf { it > 0f } ?: (160f * density)
            val dropH = layoutAlignDropdown.measuredHeight.toFloat().takeIf { it > 0f } ?: (120f * density)

            val screenW = whiteboardSurface.width.toFloat()
            val screenH = whiteboardSurface.height.toFloat()
            val minEdge = 12f * density

            val anchorX = layoutSelectionContextMenu.x + btnAlignSelection.x + (btnAlignSelection.width / 2f) - (dropW / 2f)
            val targetX = anchorX.coerceIn(minEdge, (screenW - dropW - minEdge).coerceAtLeast(minEdge))

            val aboveY = layoutSelectionContextMenu.y - dropH - (6f * density)
            val belowY = layoutSelectionContextMenu.y + layoutSelectionContextMenu.height + (6f * density)

            val targetY = if (belowY + dropH <= screenH - (80f * density)) belowY else aboveY.coerceAtLeast(minEdge)

            layoutAlignDropdown.bringToFront()
            layoutAlignDropdown.x = targetX
            layoutAlignDropdown.y = targetY
        }

        btnAlignSelection.setOnClickListener {
            if (layoutAlignDropdown.visibility == View.VISIBLE) {
                layoutAlignDropdown.visibility = View.GONE
            } else {
                positionAlignDropdown()
                layoutAlignDropdown.visibility = View.VISIBLE
            }
        }

        btnAlignBringToFront.setOnClickListener {
            whiteboardSurface.bringToFrontSelected()
            layoutAlignDropdown.visibility = View.GONE
        }

        btnAlignSendBackward.setOnClickListener {
            whiteboardSurface.sendBackwardSelected()
            layoutAlignDropdown.visibility = View.GONE
        }

        btnAlignSendToBack.setOnClickListener {
            whiteboardSurface.sendToBackSelected()
            layoutAlignDropdown.visibility = View.GONE
        }

        btnDeleteSelected.setOnClickListener {
            whiteboardSurface.deleteSelected()
            layoutSelectionContextMenu.visibility = View.GONE
            layoutAlignDropdown.visibility = View.GONE
            updatePasteButtonState()
        }

        colorBlack  = findViewById(R.id.colorBlack)
        colorWhite  = findViewById(R.id.colorWhite)
        colorBlue   = findViewById(R.id.colorBlue)
        colorRed    = findViewById(R.id.colorRed)

        // ── Main Tool Dock Selection ──────────────────────────────────────────
        btnPen.setSafeOnClickListener {
            if (activeTool == ToolType.PEN && flyoutPen.visibility == View.VISIBLE) {
                hideFlyoutAnimated(flyoutPen)
            } else {
                closeFlyouts(except = flyoutPen)
                selectTool(ToolType.PEN, btnPen)
                showFlyoutAnimated(flyoutPen)
            }
        }

        btnEraser.setSafeOnClickListener {
            if (activeTool == ToolType.ERASER_PIXEL && flyoutEraser.visibility == View.VISIBLE) {
                hideFlyoutAnimated(flyoutEraser)
                cardEraserSizeSlider.visibility = View.GONE
            } else {
                closeFlyouts(except = flyoutEraser)
                selectTool(ToolType.ERASER_PIXEL, btnEraser)
                showFlyoutAnimated(flyoutEraser)
            }
        }

        btnShapes.setSafeOnClickListener {
            if (activeTool == ToolType.SHAPE && flyoutShapes.visibility == View.VISIBLE) {
                hideFlyoutAnimated(flyoutShapes)
            } else {
                closeFlyouts(except = flyoutShapes)
                selectTool(ToolType.SHAPE, btnShapes)
                showFlyoutAnimated(flyoutShapes)
            }
        }

        btnSelect.setSafeOnClickListener {
            closeFlyouts()
            selectTool(ToolType.SELECTION, btnSelect)
        }

        btnFolder.setSafeOnClickListener {
            closeFlyouts()
            val provider = getSharedPreferences(FileManagerDialog.PREFS_NAME, Context.MODE_PRIVATE)
                .getString("file_manager_provider", "builtin") ?: "builtin"
            if (provider == "builtin") {
                if (supportFragmentManager.findFragmentByTag(FileManagerDialog.TAG) == null) {
                    FileManagerDialog.newInstance().show(supportFragmentManager, FileManagerDialog.TAG)
                }
            } else {
                val willOpen = (flyoutSystemFileMenu.visibility != View.VISIBLE)
                if (willOpen) {
                    showFlyoutAnimated(flyoutSystemFileMenu)
                } else {
                    hideFlyoutAnimated(flyoutSystemFileMenu)
                }
            }
        }

        btnCloseSystemFileMenu.setOnClickListener {
            flyoutSystemFileMenu.visibility = View.GONE
        }

        btnSysFileNew.setOnClickListener {
            flyoutSystemFileMenu.visibility = View.GONE
            onFmNewFile()
        }

        btnSysFileOpen.setOnClickListener {
            flyoutSystemFileMenu.visibility = View.GONE
            safOpenDocumentLauncher.launch(arrayOf("*/*"))
        }

        btnSysFileSave.setOnClickListener {
            flyoutSystemFileMenu.visibility = View.GONE
            val curPath = currentFilePath
            if (curPath != null && curPath.endsWith(".owb")) {
                onFmSaveFile(curPath)
            } else {
                safSaveDocumentLauncher.launch("Whiteboard_${System.currentTimeMillis()}.owb")
            }
        }

        btnSysFileSaveAs.setOnClickListener {
            flyoutSystemFileMenu.visibility = View.GONE
            safSaveDocumentLauncher.launch("Whiteboard_${System.currentTimeMillis()}.owb")
        }

        btnSysFileExportPdf.setOnClickListener {
            flyoutSystemFileMenu.visibility = View.GONE
            promptPdfExportFlow { selectedIndices ->
                pendingExportPageIndices = selectedIndices
                safExportPdfLauncher.launch("Whiteboard_Export_${System.currentTimeMillis()}.pdf")
            }
        }

        btnSysFileShare.setOnClickListener {
            flyoutSystemFileMenu.visibility = View.GONE
            promptPdfShareFlow()
        }

        btnCloseShapes.setOnClickListener {
            flyoutShapes.visibility = View.GONE
        }

        val allShapeButtons = listOf(
            btnShapeLine, btnShapeArrow, btnShapeRect, btnShapeCircle, btnShapeTriangle,
            btnShapeRightTriangle, btnShapeDiamond, btnShapeStar, btnShapeHexagon,
            btnShapeCube, btnShapeCuboid, btnShapeSphere, btnShapeCylinder,
            btnShapeCone, btnShapeFrustum, btnShapePyramid, btnShapePrism
        )

        fun highlightShape(activeBtn: ImageButton) {
            allShapeButtons.forEach { b ->
                b.setBackgroundResource(R.drawable.bg_tool_unselected)
                b.clearColorFilter()
            }
            activeBtn.setBackgroundResource(R.drawable.bg_tool_selected)
            activeBtn.clearColorFilter()
        }

        highlightShape(btnShapeRect)

        // 2D Shapes Listeners
        btnShapeLine.setOnClickListener          { whiteboardSurface.setShapeType(ShapeType.LINE);           highlightShape(btnShapeLine); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeArrow.setOnClickListener         { whiteboardSurface.setShapeType(ShapeType.ARROW);          highlightShape(btnShapeArrow); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeRect.setOnClickListener          { whiteboardSurface.setShapeType(ShapeType.RECTANGLE);      highlightShape(btnShapeRect); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeCircle.setOnClickListener        { whiteboardSurface.setShapeType(ShapeType.CIRCLE);         highlightShape(btnShapeCircle); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeTriangle.setOnClickListener      { whiteboardSurface.setShapeType(ShapeType.TRIANGLE);       highlightShape(btnShapeTriangle); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeRightTriangle.setOnClickListener { whiteboardSurface.setShapeType(ShapeType.RIGHT_TRIANGLE); highlightShape(btnShapeRightTriangle); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeDiamond.setOnClickListener       { whiteboardSurface.setShapeType(ShapeType.DIAMOND);        highlightShape(btnShapeDiamond); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeStar.setOnClickListener          { whiteboardSurface.setShapeType(ShapeType.STAR);           highlightShape(btnShapeStar); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeHexagon.setOnClickListener       { whiteboardSurface.setShapeType(ShapeType.HEXAGON);        highlightShape(btnShapeHexagon); selectTool(ToolType.SHAPE, btnShapes) }

        // 3D Shapes Listeners
        btnShapeCube.setOnClickListener     { whiteboardSurface.setShapeType(ShapeType.CUBE);     highlightShape(btnShapeCube); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeCuboid.setOnClickListener   { whiteboardSurface.setShapeType(ShapeType.CUBOID);   highlightShape(btnShapeCuboid); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeSphere.setOnClickListener   { whiteboardSurface.setShapeType(ShapeType.SPHERE);   highlightShape(btnShapeSphere); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeCylinder.setOnClickListener { whiteboardSurface.setShapeType(ShapeType.CYLINDER); highlightShape(btnShapeCylinder); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeCone.setOnClickListener     { whiteboardSurface.setShapeType(ShapeType.CONE);     highlightShape(btnShapeCone); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapeFrustum.setOnClickListener  { whiteboardSurface.setShapeType(ShapeType.FRUSTUM);  highlightShape(btnShapeFrustum); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapePyramid.setOnClickListener  { whiteboardSurface.setShapeType(ShapeType.PYRAMID);  highlightShape(btnShapePyramid); selectTool(ToolType.SHAPE, btnShapes) }
        btnShapePrism.setOnClickListener    { whiteboardSurface.setShapeType(ShapeType.PRISM);    highlightShape(btnShapePrism); selectTool(ToolType.SHAPE, btnShapes) }

        // ── Background Tool & Dual-Panel Flyout ───────────────────────────────────
        btnBgImages             = findViewById(R.id.btnBgImages)
        btnBgColorGrid          = findViewById(R.id.btnBgColorGrid)
        layoutBgDateTimeRow     = findViewById(R.id.layoutBgDateTimeRow)
        switchDateTimeOverlay   = findViewById(R.id.switchDateTimeOverlay)
        layoutDateTimeOverlay   = findViewById(R.id.layoutDateTimeOverlay)
        tvDateTimeOverlay       = findViewById(R.id.tvDateTimeOverlay)
        cardColorGridSubPanel   = findViewById(R.id.cardColorGridSubPanel)
        btnIrlenInfo            = findViewById(R.id.btnIrlenInfo)
        layoutBgCustomColorRow  = findViewById(R.id.layoutBgCustomColorRow)
        btnBgColorWheel         = findViewById(R.id.btnBgColorWheel)
        btnApplyBgAllPages      = findViewById(R.id.btnApplyBgAllPages)

        btnBackground.setSafeOnClickListener {
            if (flyoutBackground.visibility == View.VISIBLE) {
                hideFlyoutAnimated(flyoutBackground)
                cardColorGridSubPanel.visibility = View.GONE
            } else {
                closeFlyouts(except = flyoutBackground)
                showFlyoutAnimated(flyoutBackground)
            }
        }

        btnBgImages.setOnClickListener {
            pickBgImageLauncher.launch("image/*")
        }

        btnBgColorGrid.setOnClickListener {
            cardColorGridSubPanel.visibility =
                if (cardColorGridSubPanel.visibility == View.VISIBLE) View.GONE else View.VISIBLE
        }

        layoutBgDateTimeRow.setOnClickListener {
            switchDateTimeOverlay.toggle()
        }

        switchDateTimeOverlay.setOnCheckedChangeListener { _, isChecked ->
            layoutDateTimeOverlay.visibility = if (isChecked) View.VISIBLE else View.GONE
            if (isChecked) updateDateTimeText()
        }

        btnIrlenInfo.setOnClickListener {
            Toast.makeText(this, "Irlen spectral filter colors help reduce visual stress and glare", Toast.LENGTH_LONG).show()
        }

        // 10 Irlen Filter Color Swatches
        val irlenViews = listOf<View>(
            findViewById(R.id.bgIrlen01), findViewById(R.id.bgIrlen02),
            findViewById(R.id.bgIrlen03), findViewById(R.id.bgIrlen04),
            findViewById(R.id.bgIrlen05), findViewById(R.id.bgIrlen06),
            findViewById(R.id.bgIrlen07), findViewById(R.id.bgIrlen08),
            findViewById(R.id.bgIrlen09), findViewById(R.id.bgIrlen10)
        )
        val irlenColors = listOf(
            Color.parseColor("#FDE2C8"), Color.parseColor("#F3C5C5"),
            Color.parseColor("#FCE588"), Color.parseColor("#FEF9A7"),
            Color.parseColor("#B9F5AA"), Color.parseColor("#A6FBE2"),
            Color.parseColor("#B1D0FD"), Color.parseColor("#CCB4FD"),
            Color.parseColor("#E2EDFD"), Color.parseColor("#DFDFDF")
        )
        irlenViews.forEachIndexed { i, v ->
            v.setOnClickListener {
                currentSelectedBgColor = irlenColors[i]
                whiteboardSurface.setPageBackgroundColor(currentSelectedBgColor)
            }
        }

        // 10 General Color Swatches
        val genViews = listOf<View>(
            findViewById(R.id.bgGen01), findViewById(R.id.bgGen02),
            findViewById(R.id.bgGen03), findViewById(R.id.bgGen04),
            findViewById(R.id.bgGen05), findViewById(R.id.bgGen06),
            findViewById(R.id.bgGen07), findViewById(R.id.bgGen08),
            findViewById(R.id.bgGen09), findViewById(R.id.bgGen10)
        )
        val genColors = listOf(
            Color.parseColor("#FFFFFF"), Color.parseColor("#F8F9FA"),
            Color.parseColor("#1C1C1E"), Color.parseColor("#2D483A"),
            Color.parseColor("#4F795E"), Color.parseColor("#FF453A"),
            Color.parseColor("#FF9500"), Color.parseColor("#FFCC00"),
            Color.parseColor("#0040DD"), Color.parseColor("#7B00DD")
        )
        genViews.forEachIndexed { i, v ->
            v.setOnClickListener {
                currentSelectedBgColor = genColors[i]
                whiteboardSurface.setPageBackgroundColor(currentSelectedBgColor)
            }
        }

        // Background Color Wheel Trigger
        val openBgColorPicker = View.OnClickListener {
            closeFlyouts()
            isColorPickerForSelection = false
            isColorPickerForBackground = true
            modalColorPickerOverlay.visibility = View.VISIBLE
            colorWheelCanvas.setColor(currentSelectedBgColor)
            viewLiveColorPreview.backgroundTintList = ColorStateList.valueOf(currentSelectedBgColor)
            etHexInput.setText(String.format("#%06X", 0xFFFFFF and currentSelectedBgColor))
        }
        layoutBgCustomColorRow.setOnClickListener(openBgColorPicker)
        btnBgColorWheel.setOnClickListener(openBgColorPicker)

        // Apply to all pages Button
        btnApplyBgAllPages.setOnClickListener {
            whiteboardSurface.setAllPagesBackground(currentSelectedBgColor, currentSelectedGridType)
            Toast.makeText(this, "Applied to all pages", Toast.LENGTH_SHORT).show()
        }

        // Start Live Date & Time timer
        updateDateTimeText()
        dateTimeHandler.postDelayed(dateTimeRunnable, 10000)

        // ── 10. Comprehensive Two-Pane Settings Window Setup ─────────────────
        val btnSettingsCloseTop: ImageButton = findViewById(R.id.btnSettingsCloseTop)
        val btnSettingsReset: Button         = findViewById(R.id.btnSettingsReset)
        val btnSettingsManageStorage: Button = findViewById(R.id.btnSettingsManageStorage)

        val navSettingsUi: LinearLayout     = findViewById(R.id.navSettingsUi)
        val navSettingsCanvas: LinearLayout = findViewById(R.id.navSettingsCanvas)
        val navSettingsPen: LinearLayout    = findViewById(R.id.navSettingsPen)
        val navSettingsFile: LinearLayout   = findViewById(R.id.navSettingsFile)
        val navSettingsTouch: LinearLayout  = findViewById(R.id.navSettingsTouch)
        val navSettingsAbout: LinearLayout  = findViewById(R.id.navSettingsAbout)

        val ivNavUi: ImageView     = findViewById(R.id.ivNavUi)
        val ivNavCanvas: ImageView = findViewById(R.id.ivNavCanvas)
        val ivNavPen: ImageView    = findViewById(R.id.ivNavPen)
        val ivNavFile: ImageView   = findViewById(R.id.ivNavFile)
        val ivNavTouch: ImageView  = findViewById(R.id.ivNavTouch)
        val ivNavAbout: ImageView  = findViewById(R.id.ivNavAbout)

        val tvNavUi: TextView     = findViewById(R.id.tvNavUi)
        val tvNavCanvas: TextView = findViewById(R.id.tvNavCanvas)
        val tvNavPen: TextView    = findViewById(R.id.tvNavPen)
        val tvNavFile: TextView   = findViewById(R.id.tvNavFile)
        val tvNavTouch: TextView  = findViewById(R.id.tvNavTouch)
        val tvNavAbout: TextView  = findViewById(R.id.tvNavAbout)

        val scrollSectionUi: View     = findViewById(R.id.scrollSectionUi)
        val scrollSectionCanvas: View = findViewById(R.id.scrollSectionCanvas)
        val scrollSectionPen: View    = findViewById(R.id.scrollSectionPen)
        val scrollSectionFile: View   = findViewById(R.id.scrollSectionFile)
        val scrollSectionTouch: View  = findViewById(R.id.scrollSectionTouch)
        val scrollSectionAbout: View  = findViewById(R.id.scrollSectionAbout)

        val switchSettingsDateTime: SwitchCompat = findViewById(R.id.switchSettingsDateTime)
        val btnThemeLight: Button  = findViewById(R.id.btnThemeLight)
        val btnThemeDark: Button   = findViewById(R.id.btnThemeDark)
        val btnThemeSystem: Button = findViewById(R.id.btnThemeSystem)
        val btnSmoothLow: Button   = findViewById(R.id.btnSmoothLow)
        val btnSmoothMed: Button   = findViewById(R.id.btnSmoothMed)
        val btnSmoothHigh: Button  = findViewById(R.id.btnSmoothHigh)
        val btnAutoSave30s: Button = findViewById(R.id.btnAutoSave30s)
        val btnAutoSave60s: Button = findViewById(R.id.btnAutoSave60s)
        val btnAutoSaveOff: Button = findViewById(R.id.btnAutoSaveOff)

        val navItems = listOf(
            Triple(navSettingsUi, ivNavUi, tvNavUi) to scrollSectionUi,
            Triple(navSettingsCanvas, ivNavCanvas, tvNavCanvas) to scrollSectionCanvas,
            Triple(navSettingsPen, ivNavPen, tvNavPen) to scrollSectionPen,
            Triple(navSettingsFile, ivNavFile, tvNavFile) to scrollSectionFile,
            Triple(navSettingsTouch, ivNavTouch, tvNavTouch) to scrollSectionTouch,
            Triple(navSettingsAbout, ivNavAbout, tvNavAbout) to scrollSectionAbout
        )

        fun updateSegmentedButton(btn: Button, isActive: Boolean) {
            btn.backgroundTintList = null
            btn.setBackgroundResource(if (isActive) R.drawable.bg_settings_segment_active else R.drawable.bg_settings_segment_inactive)
            btn.setTextColor(Color.parseColor(if (isActive) "#0F172A" else "#64748B"))
            btn.setTypeface(null, if (isActive) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
        }

        fun selectSettingsCategory(selectedNav: LinearLayout) {
            navItems.forEach { (triple, section) ->
                val (nav, iv, tv) = triple
                val isSel = (nav == selectedNav)
                nav.setBackgroundResource(if (isSel) R.drawable.bg_settings_nav_active else R.drawable.bg_settings_nav_normal)
                iv.setColorFilter(Color.parseColor(if (isSel) "#0F172A" else "#64748B"))
                tv.setTextColor(Color.parseColor(if (isSel) "#0F172A" else "#64748B"))
                tv.setTypeface(null, if (isSel) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
                section.visibility = if (isSel) View.VISIBLE else View.GONE
            }
        }

        val isCompactScreen = resources.getBoolean(R.bool.is_compact_screen)
        val tvSettingsSubtitle: TextView? = findViewById(R.id.tvSettingsSubtitle)
        val cardSettingsWindow: LinearLayout? = findViewById(R.id.cardSettingsWindow)
        if (isCompactScreen) {
            tvSettingsSubtitle?.visibility = View.GONE
            val navTextViews = listOf(tvNavUi, tvNavCanvas, tvNavPen, tvNavFile, tvNavTouch, tvNavAbout)
            navTextViews.forEach { it.visibility = View.GONE }
            val navContainers = listOf(navSettingsUi, navSettingsCanvas, navSettingsPen, navSettingsFile, navSettingsTouch, navSettingsAbout)
            navContainers.forEach { nav ->
                nav.gravity = android.view.Gravity.CENTER
                nav.setPadding(0, 0, 0, 0)
            }
            if (cardSettingsWindow != null) {
                val lp = cardSettingsWindow.layoutParams as? FrameLayout.LayoutParams
                if (lp != null) {
                    lp.width = ViewGroup.LayoutParams.MATCH_PARENT
                    lp.height = ViewGroup.LayoutParams.MATCH_PARENT
                    val m = resources.getDimensionPixelSize(R.dimen.dialog_settings_margin)
                    lp.setMargins(m, m, m, m)
                    cardSettingsWindow.layoutParams = lp
                }
            }
        }

        navSettingsUi.setOnClickListener     { selectSettingsCategory(navSettingsUi) }
        navSettingsCanvas.setOnClickListener { selectSettingsCategory(navSettingsCanvas) }
        navSettingsPen.setOnClickListener    { selectSettingsCategory(navSettingsPen) }
        navSettingsFile.setOnClickListener   { selectSettingsCategory(navSettingsFile) }
        navSettingsTouch.setOnClickListener  { selectSettingsCategory(navSettingsTouch) }
        navSettingsAbout.setOnClickListener  { selectSettingsCategory(navSettingsAbout) }

        btnSettings.setSafeOnClickListener {
            if (modalSettingsOverlay.visibility == View.VISIBLE) {
                hideFlyoutAnimated(modalSettingsOverlay)
            } else {
                closeFlyouts(except = modalSettingsOverlay)
                switchSettingsDateTime.isChecked = switchDateTimeOverlay.isChecked
                selectSettingsCategory(navSettingsUi)
                showFlyoutAnimated(modalSettingsOverlay)
            }
        }

        btnSettingsCloseTop.setSafeOnClickListener { hideFlyoutAnimated(modalSettingsOverlay) }
        btnCloseSettings.setSafeOnClickListener    { hideFlyoutAnimated(modalSettingsOverlay) }

        switchSettingsDateTime.setOnCheckedChangeListener { _, isChecked ->
            switchDateTimeOverlay.isChecked = isChecked
        }

        btnSettingsManageStorage.setOnClickListener {
            checkAndRequestStorageManagerPermission()
        }

        btnSettingsReset.setOnClickListener {
            Toast.makeText(this, "Settings restored to system defaults", Toast.LENGTH_SHORT).show()
        }

        // Theme buttons
        val themeBtns = listOf(btnThemeLight, btnThemeDark, btnThemeSystem)
        fun selectThemeButton(activeBtn: Button) {
            themeBtns.forEach { b ->
                updateSegmentedButton(b, b == activeBtn)
            }
        }
        btnThemeLight.setOnClickListener  { selectThemeButton(btnThemeLight) }
        btnThemeDark.setOnClickListener   { selectThemeButton(btnThemeDark); Toast.makeText(this, "Dark mode preference saved", Toast.LENGTH_SHORT).show() }
        btnThemeSystem.setOnClickListener { selectThemeButton(btnThemeSystem) }
        selectThemeButton(btnThemeLight)

        // Slide Size & Aspect Ratio Presets Setup
        setupSlidePresetsAndCustomSize()

        // Smoothing buttons
        val smoothBtns = listOf(btnSmoothLow, btnSmoothMed, btnSmoothHigh)
        fun selectSmoothButton(activeBtn: Button) {
            smoothBtns.forEach { b ->
                updateSegmentedButton(b, b == activeBtn)
            }
        }
        btnSmoothLow.setOnClickListener  { selectSmoothButton(btnSmoothLow) }
        btnSmoothMed.setOnClickListener  { selectSmoothButton(btnSmoothMed) }
        btnSmoothHigh.setOnClickListener { selectSmoothButton(btnSmoothHigh) }
        selectSmoothButton(btnSmoothMed)

        // Auto-Save buttons
        val autoSaveBtns = listOf(btnAutoSave30s, btnAutoSave60s, btnAutoSaveOff)
        fun selectAutoSaveButton(activeBtn: Button) {
            autoSaveBtns.forEach { b ->
                updateSegmentedButton(b, b == activeBtn)
            }
        }
        btnAutoSave30s.setOnClickListener { selectAutoSaveButton(btnAutoSave30s) }
        btnAutoSave60s.setOnClickListener { selectAutoSaveButton(btnAutoSave60s) }
        btnAutoSaveOff.setOnClickListener { selectAutoSaveButton(btnAutoSaveOff) }
        selectAutoSaveButton(btnAutoSave30s)

        btnModeFixedPage.setOnClickListener {
            isFixedCanvasMode = true
            updateCanvasModeUi()
        }

        btnModeOpenCanvas.setOnClickListener {
            isFixedCanvasMode = false
            updateCanvasModeUi()
        }

        updateCanvasModeUi()
        setupSlidePresetsAndCustomSize()

        // ── Default Slide / PDF Import Mode Setting ──────────────────────────
        val fmPrefs = getSharedPreferences(FileManagerDialog.PREFS_NAME, Context.MODE_PRIVATE)
        fun updateImportModeSettingsUi() {
            val mode = fmPrefs.getString(FileManagerDialog.PREF_DEFAULT_IMPORT_MODE, "background") ?: "background"
            updateSegmentedButton(btnSettingImportBg, mode == "background")
            updateSegmentedButton(btnSettingImportObj, mode != "background")
        }
        updateImportModeSettingsUi()

        btnSettingImportBg.setOnClickListener {
            fmPrefs.edit().putString(FileManagerDialog.PREF_DEFAULT_IMPORT_MODE, "background").apply()
            updateImportModeSettingsUi()
        }

        btnSettingImportObj.setOnClickListener {
            fmPrefs.edit().putString(FileManagerDialog.PREF_DEFAULT_IMPORT_MODE, "object").apply()
            updateImportModeSettingsUi()
        }

        // ── Storage & File Manager Provider Setting ──────────────────────────
        val btnProviderBuiltIn: Button = findViewById(R.id.btnProviderBuiltIn)
        val btnProviderSystem: Button  = findViewById(R.id.btnProviderSystem)

        fun updateStorageProviderUi() {
            val prov = fmPrefs.getString("file_manager_provider", "builtin") ?: "builtin"
            updateSegmentedButton(btnProviderBuiltIn, prov == "builtin")
            updateSegmentedButton(btnProviderSystem, prov != "builtin")
        }
        updateStorageProviderUi()

        btnProviderBuiltIn.setOnClickListener {
            fmPrefs.edit().putString("file_manager_provider", "builtin").apply()
            updateStorageProviderUi()
            Toast.makeText(this, "File Manager set to Built-in Explorer", Toast.LENGTH_SHORT).show()
        }

        btnProviderSystem.setOnClickListener {
            fmPrefs.edit().putString("file_manager_provider", "system").apply()
            updateStorageProviderUi()
            Toast.makeText(this, "File Manager set to Android System SAF", Toast.LENGTH_SHORT).show()
        }

        // ── Pen Types Row (7 Specialized Pen & Brush Types) ───────────────────
        val penTypeBtns = listOf(
            flyPenStandard, flyPenBrush, flyPenHighlighter,
            flyPenStamp, flyPenLaser, flyPenShapeStamp, flyPenGlitter
        )

        fun highlightPenType(activeBtn: ImageButton) {
            penTypeBtns.forEach { b ->
                b.setBackgroundResource(R.drawable.bg_tool_unselected)
                b.clearColorFilter()
            }
            activeBtn.setBackgroundResource(R.drawable.bg_tool_selected)
            activeBtn.clearColorFilter()
        }

        flyPenStandard.setOnClickListener {
            whiteboardSurface.setActiveTool(ToolType.PEN)
            whiteboardSurface.setPenType(0)
            whiteboardSurface.setStrokeWidth(4f)
            whiteboardSurface.setStrokeOpacity(1f)
            highlightPenType(flyPenStandard)
        }

        flyPenBrush.setOnClickListener {
            whiteboardSurface.setActiveTool(ToolType.PEN)
            whiteboardSurface.setPenType(1)
            whiteboardSurface.setStrokeWidth(6f)
            whiteboardSurface.setStrokeOpacity(1f)
            highlightPenType(flyPenBrush)
        }

        flyPenHighlighter.setOnClickListener {
            whiteboardSurface.setActiveTool(ToolType.HIGHLIGHTER)
            whiteboardSurface.setPenType(2)
            whiteboardSurface.setStrokeWidth(24f)
            whiteboardSurface.setStrokeOpacity(0.4f)
            highlightPenType(flyPenHighlighter)
        }

        flyPenStamp.setOnClickListener {
            whiteboardSurface.setActiveTool(ToolType.PEN)
            whiteboardSurface.setPenType(0)
            whiteboardSurface.setStrokeWidth(12f)
            whiteboardSurface.setStrokeOpacity(1f)
            highlightPenType(flyPenStamp)
        }

        flyPenLaser.setOnClickListener {
            whiteboardSurface.setActiveTool(ToolType.PEN)
            whiteboardSurface.setPenType(3)
            whiteboardSurface.setStrokeWidth(8f)
            whiteboardSurface.setStrokeOpacity(1f)
            highlightPenType(flyPenLaser)
        }

        flyPenShapeStamp.setOnClickListener {
            whiteboardSurface.setActiveTool(ToolType.PEN)
            whiteboardSurface.setPenType(0)
            whiteboardSurface.setStrokeWidth(14f)
            whiteboardSurface.setStrokeOpacity(1f)
            highlightPenType(flyPenShapeStamp)
        }

        flyPenGlitter.setOnClickListener {
            whiteboardSurface.setActiveTool(ToolType.PEN)
            whiteboardSurface.setPenType(3)
            whiteboardSurface.setStrokeWidth(10f)
            whiteboardSurface.setStrokeOpacity(1f)
            highlightPenType(flyPenGlitter)
        }

        // ── Stroke Thickness Presets (Thin, Medium, Thick) ────────────────────
        val widthPills = listOf(flyWidthThin, flyWidthMedium, flyWidthThick)
        fun highlightWidth(activePill: ImageButton, width: Float) {
            whiteboardSurface.setStrokeWidth(width)
            widthPills.forEach { p ->
                p.setBackgroundResource(R.drawable.bg_pill_unselected)
                p.setColorFilter(Color.parseColor("#4A4A4F"))
            }
            activePill.setBackgroundResource(R.drawable.bg_pill_selected)
            activePill.setColorFilter(Color.WHITE)
        }

        flyWidthThin.setOnClickListener   { highlightWidth(flyWidthThin, 3f) }
        flyWidthMedium.setOnClickListener { highlightWidth(flyWidthMedium, 7f) }
        flyWidthThick.setOnClickListener  { highlightWidth(flyWidthThick, 16f) }

        // ── Quick Colors with Caret Indicators ────────────────────────────────
        val carets = listOf(caretBlack, caretWhite, caretRed, caretBlue, caretCustom)
        fun selectColorSwatch(argb: Int, activeCaret: View) {
            whiteboardSurface.setStrokeColor(argb)
            carets.forEach { c ->
                c.visibility = if (c == activeCaret) View.VISIBLE else View.INVISIBLE
            }
        }

        colorBlack.setOnClickListener { selectColorSwatch(Color.BLACK, caretBlack) }
        colorWhite.setOnClickListener { selectColorSwatch(Color.WHITE, caretWhite) }
        colorRed.setOnClickListener   { selectColorSwatch(Color.parseColor("#FF3B30"), caretRed) }
        colorBlue.setOnClickListener  { selectColorSwatch(Color.parseColor("#0A84FF"), caretBlue) }

        // ── Professional Color Wheel Spectrum & Honeycomb Palette Controls ─────
        colorWheelCanvas.onColorChangedListener = { color ->
            activeCustomColor = color
            viewLiveColorPreview.backgroundTintList = ColorStateList.valueOf(color)
            etHexInput.setText(String.format("#%06X", 0xFFFFFF and color))
            if (isColorPickerForSelection) {
                whiteboardSurface.setSelectedColor(color)
                viewSelectionColorDot.backgroundTintList = ColorStateList.valueOf(color)
            } else if (isColorPickerForBackground) {
                currentSelectedBgColor = color
                whiteboardSurface.setPageBackgroundColor(color)
            }
        }

        btnColorWheelPicker.setOnClickListener {
            closeFlyouts()
            isColorPickerForSelection = false
            isColorPickerForBackground = false
            selectColorSwatch(activeCustomColor, caretCustom)
            modalColorPickerOverlay.visibility = View.VISIBLE
            colorWheelCanvas.setColor(activeCustomColor)
            viewLiveColorPreview.backgroundTintList = ColorStateList.valueOf(activeCustomColor)
            etHexInput.setText(String.format("#%06X", 0xFFFFFF and activeCustomColor))
        }

        etHexInput.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: android.text.Editable?) {
                val hex = s?.toString()?.trim() ?: ""
                if (hex.length == 7 && hex.startsWith("#")) {
                    try {
                        val parsed = Color.parseColor(hex)
                        activeCustomColor = parsed
                        colorWheelCanvas.setColor(parsed)
                        viewLiveColorPreview.backgroundTintList = ColorStateList.valueOf(parsed)
                        if (isColorPickerForSelection) {
                            whiteboardSurface.setSelectedColor(parsed)
                            viewSelectionColorDot.backgroundTintList = ColorStateList.valueOf(parsed)
                        } else if (isColorPickerForBackground) {
                            currentSelectedBgColor = parsed
                            whiteboardSurface.setPageBackgroundColor(parsed)
                        }
                    } catch (_: Throwable) {}
                }
            }
        })

        seekBarVal.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) colorWheelCanvas.setVal(progress / 100f)
            }
            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })

        modalColorPickerOverlay.setOnClickListener {
            modalColorPickerOverlay.visibility = View.GONE
            isColorPickerForSelection = false
            isColorPickerForBackground = false
        }

        btnCancelColorPicker.setOnClickListener {
            modalColorPickerOverlay.visibility = View.GONE
            isColorPickerForSelection = false
            isColorPickerForBackground = false
        }

        btnApplyCustomColor.setOnClickListener {
            try {
                val hexStr = etHexInput.text.toString().trim()
                if (hexStr.isNotEmpty()) {
                    val parsed = Color.parseColor(if (hexStr.startsWith("#")) hexStr else "#$hexStr")
                    activeCustomColor = parsed
                }
            } catch (e: Throwable) {
                Log.e(TAG, "Invalid hex color", e)
            }
            if (isColorPickerForSelection) {
                whiteboardSurface.setSelectedColor(activeCustomColor)
                viewSelectionColorDot.backgroundTintList = ColorStateList.valueOf(activeCustomColor)
                isColorPickerForSelection = false
            } else if (isColorPickerForBackground) {
                currentSelectedBgColor = activeCustomColor
                whiteboardSurface.setPageBackgroundColor(activeCustomColor)
                isColorPickerForBackground = false
            } else {
                selectColorSwatch(activeCustomColor, caretCustom)
            }
            modalColorPickerOverlay.visibility = View.GONE
        }

        // ── Eraser Flyout Sub-Items ───────────────────────────────────────────
        val eraserButtons = listOf(btnEraserNormal, btnEraserObject)
        var activeEraserMode = 0

        fun selectEraserMode(mode: Int, activeBtn: ImageButton) {
            activeEraserMode = mode
            whiteboardSurface.setActiveTool(ToolType.ERASER_PIXEL)
            whiteboardSurface.setEraserMode(mode)
            eraserButtons.forEach { b ->
                b.setBackgroundResource(R.drawable.bg_tool_unselected)
            }
            activeBtn.setBackgroundResource(R.drawable.bg_tool_selected)
        }

        btnEraserNormal.setOnClickListener {
            if (activeEraserMode == 0 && flyoutEraser.visibility == View.VISIBLE) {
                // Secondary click on Normal Eraser: Toggle Eraser Size slider sub-popup!
                cardEraserSizeSlider.visibility = if (cardEraserSizeSlider.visibility == View.VISIBLE) View.GONE else View.VISIBLE
            } else {
                selectEraserMode(0, btnEraserNormal)
                cardEraserSizeSlider.visibility = View.GONE
            }
        }

        btnEraserObject.setOnClickListener {
            selectEraserMode(1, btnEraserObject)
            cardEraserSizeSlider.visibility = View.GONE
        }

        btnClearPage.setOnClickListener {
            whiteboardSurface.clearActivePage()
            cardEraserSizeSlider.visibility = View.GONE
        }

        seekBarEraserSize.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                val radius = progress.coerceAtLeast(8).toFloat()
                whiteboardSurface.setEraserSize(radius)
            }
            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })



        // ── Page Navigation, Zoom, Undo, Redo ─────────────────────────────────
        btnPrevPage.setSafeOnClickListener {
            val curr = whiteboardSurface.getActivePageIndex()
            if (curr > 0) {
                val target = curr - 1
                whiteboardSurface.setActivePage(target)
                updatePageInfo()
                updatePasteButtonState()
                checkAndShowPageLoadingIndicator(target)
            }
        }

        btnNextPage.setSafeOnClickListener {
            val curr  = whiteboardSurface.getActivePageIndex()
            val total = whiteboardSurface.getPageCount()
            if (curr + 1 < total) {
                val target = curr + 1
                whiteboardSurface.setActivePage(target)
                updatePageInfo()
                updatePasteButtonState()
                checkAndShowPageLoadingIndicator(target)
            }
        }

        btnAddPage.setSafeOnClickListener {
            whiteboardSurface.addPage()
            updatePageInfo()
            updatePasteButtonState()
        }
        btnFitPage.setSafeOnClickListener { whiteboardSurface.fitPageToScreen() }
        btnUndo.setSafeOnClickListener    { whiteboardSurface.undo() }
        btnRedo.setSafeOnClickListener    { whiteboardSurface.redo() }

        // ── Page Manager Panel Setup ──────────────────────────────────────────
        flyoutPageManager       = findViewById(R.id.flyoutPageManager)
        tvPageManagerTitle      = findViewById(R.id.tvPageManagerTitle)
        tvPageManagerCount      = findViewById(R.id.tvPageManagerCount)
        rvPages                 = findViewById(R.id.rvPages)
        layoutPageSelectionPill = findViewById(R.id.layoutPageSelectionPill)
        tvPageSelectionCount    = findViewById(R.id.tvPageSelectionCount)
        btnClearPageSelection   = findViewById(R.id.btnClearPageSelection)
        btnDeleteSelectedPages  = findViewById(R.id.btnDeleteSelectedPages)
        btnSelectAllPages       = findViewById(R.id.btnSelectAllPages)
        dividerBatchActions     = findViewById(R.id.dividerBatchActions)
        btnPinPageManager       = findViewById(R.id.btnPinPageManager)
        btnExpandPageManager    = findViewById(R.id.btnExpandPageManager)
        btnClosePageManager     = findViewById(R.id.btnClosePageManager)

        rvPages.layoutManager = LinearLayoutManager(this)
        pageAdapter = PageManagerAdapter(this, lifecycleScope, whiteboardSurface, object : PageManagerAdapter.Callbacks {
            override fun onPageSelected(index: Int) {
                whiteboardSurface.setActivePage(index)
                updatePageInfo()
                updatePasteButtonState()
                checkAndShowPageLoadingIndicator(index)
                if (!isPageManagerPinned && !isPageManagerExpanded) {
                    flyoutPageManager.visibility = View.GONE
                }
            }

            override fun onAddPageRequested() {
                whiteboardSurface.addPage()
                updatePageInfo()
                updatePasteButtonState()
            }

            override fun onPageDuplicate(index: Int) {
                whiteboardSurface.duplicatePage(index)
                updatePageInfo()
                updatePasteButtonState()
            }

            override fun onPageInsertBefore(index: Int) {
                whiteboardSurface.insertPage(index)
                updatePageInfo()
                updatePasteButtonState()
            }

            override fun onPageInsertAfter(index: Int) {
                whiteboardSurface.insertPage(index + 1)
                updatePageInfo()
                updatePasteButtonState()
            }

            override fun onPageClear(index: Int) {
                whiteboardSurface.setActivePage(index)
                whiteboardSurface.clearActivePage()
                pageAdapter.invalidateThumbnail(index)
                updatePageInfo()
            }

            override fun onPageDelete(index: Int) {
                whiteboardSurface.deletePage(index)
                updatePageInfo()
                updatePasteButtonState()
                pageAdapter.invalidateThumbnails()
            }

            override fun onPageReordered(fromIdx: Int, toIdx: Int) {
                updatePageInfo()
                updatePasteButtonState()
            }

            override fun onSelectionCountChanged(count: Int) {
                if (count > 0) {
                    layoutPageSelectionPill.visibility = View.VISIBLE
                    tvPageSelectionCount.text = "$count pages selected"
                    btnDeleteSelectedPages.visibility = View.VISIBLE
                    btnSelectAllPages.visibility = View.VISIBLE
                    dividerBatchActions.visibility = View.VISIBLE
                } else {
                    layoutPageSelectionPill.visibility = View.GONE
                    btnDeleteSelectedPages.visibility = View.GONE
                    btnSelectAllPages.visibility = View.GONE
                    dividerBatchActions.visibility = View.GONE
                }
            }
        })
        rvPages.adapter = pageAdapter

        val itemTouchHelper = ItemTouchHelper(object : ItemTouchHelper.SimpleCallback(
            ItemTouchHelper.UP or ItemTouchHelper.DOWN or ItemTouchHelper.LEFT or ItemTouchHelper.RIGHT, 0
        ) {
            override fun onMove(
                recyclerView: RecyclerView,
                viewHolder: RecyclerView.ViewHolder,
                target: RecyclerView.ViewHolder
            ): Boolean {
                val fromPos = viewHolder.adapterPosition
                val toPos = target.adapterPosition
                val count = whiteboardSurface.getPageCount()
                if (fromPos < 0 || toPos < 0 || fromPos >= count || toPos >= count) return false
                pageAdapter.onItemMove(fromPos, toPos)
                return true
            }

            override fun onSwiped(viewHolder: RecyclerView.ViewHolder, direction: Int) {}
        })
        itemTouchHelper.attachToRecyclerView(rvPages)

        // Click on page indicator in bottom dock toggles Page Manager panel
        tvPageInfo.setOnClickListener {
            if (flyoutPageManager.visibility == View.VISIBLE) {
                flyoutPageManager.visibility = View.GONE
            } else {
                closeFlyouts()
                val density = resources.displayMetrics.density
                val lp = flyoutPageManager.layoutParams as RelativeLayout.LayoutParams
                if (!isPageManagerExpanded) {
                    val maxStripH = (resources.displayMetrics.heightPixels - (100 * density)).toInt()
                    lp.width = (220 * density).toInt()
                    lp.height = (560 * density).toInt().coerceAtMost(maxStripH)
                    flyoutPageManager.layoutParams = lp
                }
                flyoutPageManager.visibility = View.VISIBLE
                updatePageInfo()
                pageAdapter.invalidateThumbnails()
            }
        }

        btnPinPageManager.setOnClickListener {
            isPageManagerPinned = !isPageManagerPinned
            btnPinPageManager.setImageResource(
                if (isPageManagerPinned) R.drawable.ic_pin_filled else R.drawable.ic_pin
            )
        }

        btnExpandPageManager.setOnClickListener {
            isPageManagerExpanded = !isPageManagerExpanded
            val density = resources.displayMetrics.density
            val lp = flyoutPageManager.layoutParams as RelativeLayout.LayoutParams

            if (isPageManagerExpanded) {
                // Expanded Grid Modal Mode (88% width, 82% height, 5 columns for maximum visible slides)
                lp.width = (resources.displayMetrics.widthPixels * 0.88f).toInt()
                lp.height = (resources.displayMetrics.heightPixels * 0.82f).toInt()
                lp.removeRule(RelativeLayout.ABOVE)
                lp.removeRule(RelativeLayout.ALIGN_START)
                lp.addRule(RelativeLayout.CENTER_IN_PARENT, RelativeLayout.TRUE)
                rvPages.layoutManager = GridLayoutManager(this, 5)
                btnExpandPageManager.setImageResource(R.drawable.ic_collapse)
            } else {
                // Vertical Strip Dock Mode (220dp width, tall height, 1 column)
                val maxStripH = (resources.displayMetrics.heightPixels - (100 * density)).toInt()
                lp.width = (220 * density).toInt()
                lp.height = (560 * density).toInt().coerceAtMost(maxStripH)
                lp.removeRule(RelativeLayout.CENTER_IN_PARENT)
                lp.addRule(RelativeLayout.ABOVE, R.id.dockPageNav)
                lp.addRule(RelativeLayout.ALIGN_START, R.id.dockPageNav)
                rvPages.layoutManager = LinearLayoutManager(this)
                btnExpandPageManager.setImageResource(R.drawable.ic_grid)
            }
            flyoutPageManager.layoutParams = lp
            pageAdapter.notifyDataSetChanged()
        }

        btnClosePageManager.setSafeOnClickListener {
            hideFlyoutAnimated(flyoutPageManager)
        }

        btnClearPageSelection.setSafeOnClickListener {
            pageAdapter.clearSelection()
        }

        btnSelectAllPages.setSafeOnClickListener {
            if (pageAdapter.selectedIndices.size == whiteboardSurface.getPageCount()) {
                pageAdapter.clearSelection()
            } else {
                pageAdapter.selectAll()
            }
        }

        btnDeleteSelectedPages.setSafeOnClickListener {
            val toDelete = pageAdapter.selectedIndices.sortedDescending().toList()
            val total = whiteboardSurface.getPageCount()
            if (toDelete.size >= total) {
                // Delete all pages -> clean fresh new page
                isImportCancelled = true
                isStreamingImportActive = false
                streamingSlideIndices.clear()
                layoutCanvasPageLoading.visibility = View.GONE
                layoutStreamLoadingBadge.visibility = View.GONE
                val (defW, defH) = getDefaultSlideDimensions()
                whiteboardSurface.resetDocumentPages(1, defW, defH)
            } else {
                for (idx in toDelete) {
                    streamingSlideIndices.remove(idx)
                    whiteboardSurface.deletePage(idx)
                }
            }
            layoutCanvasPageLoading.visibility = View.GONE
            pageAdapter.clearSelection()
            pageAdapter.invalidateThumbnails()
            updatePageInfo()
            updatePasteButtonState()
        }

        tvPageInfo.setSafeOnClickListener {
            if (flyoutPageManager.visibility == View.VISIBLE) {
                hideFlyoutAnimated(flyoutPageManager)
            } else {
                closeFlyouts(except = flyoutPageManager)
                showFlyoutAnimated(flyoutPageManager)
                pageAdapter.notifyDataSetChanged()
            }
        }

        updatePageInfo()
    }

    private fun selectTool(tool: Int, selectedBtn: ImageButton) {
        activeTool = tool
        whiteboardSurface.setActiveTool(tool)
        if (tool != -1) {
            whiteboardSurface.clearSelection()
            layoutSelectionContextMenu.visibility = View.GONE
        }
        if (tool != ToolType.SELECTION) {
            layoutTapPastePopup.visibility = View.GONE
        }

        val allBtns = listOf(
            btnSelect, btnPen, btnEraser, btnShapes, btnFolder
        )
        allBtns.forEach { b ->
            b.setBackgroundResource(R.drawable.bg_tool_unselected)
            b.clearColorFilter()
        }

        selectedBtn.setBackgroundResource(R.drawable.bg_tool_selected)
        selectedBtn.clearColorFilter()
    }

    private fun updateCanvasModeUi() {
        if (isFixedCanvasMode) {
            // Fixed Slide Mode (Default): Minimap and Fit Page buttons are REMOVED from toolbar!
            btnMinimap.visibility = View.GONE
            btnFitPage.visibility = View.GONE
            dividerSettings.visibility = View.GONE

            // Slide Navigation stays visible for Fixed Slide mode
            dockPageNav.visibility = View.VISIBLE
            btnModeFixedPage.backgroundTintList = null
            btnModeFixedPage.setBackgroundResource(if (isFixedCanvasMode) R.drawable.bg_settings_segment_active else R.drawable.bg_settings_segment_inactive)
            btnModeFixedPage.setTextColor(Color.parseColor(if (isFixedCanvasMode) "#0F172A" else "#64748B"))
            btnModeFixedPage.setTypeface(null, if (isFixedCanvasMode) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)

            btnModeOpenCanvas.backgroundTintList = null
            btnModeOpenCanvas.setBackgroundResource(if (!isFixedCanvasMode) R.drawable.bg_settings_segment_active else R.drawable.bg_settings_segment_inactive)
            btnModeOpenCanvas.setTextColor(Color.parseColor(if (!isFixedCanvasMode) "#0F172A" else "#64748B"))
            btnModeOpenCanvas.setTypeface(null, if (!isFixedCanvasMode) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
        } else {
            // Open Canvas Mode: Minimap and Fit Page buttons appear next to Settings!
            btnMinimap.visibility = View.VISIBLE
            btnFitPage.visibility = View.VISIBLE
            dividerSettings.visibility = View.VISIBLE

            // Slide Navigation hidden in Open Canvas mode
            dockPageNav.visibility = View.GONE
            layoutSlideSizeSettings?.visibility = View.GONE

            btnModeFixedPage.backgroundTintList = null
            btnModeFixedPage.setBackgroundResource(if (isFixedCanvasMode) R.drawable.bg_settings_segment_active else R.drawable.bg_settings_segment_inactive)
            btnModeFixedPage.setTextColor(Color.parseColor(if (isFixedCanvasMode) "#0F172A" else "#64748B"))
            btnModeFixedPage.setTypeface(null, if (isFixedCanvasMode) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)

            btnModeOpenCanvas.backgroundTintList = null
            btnModeOpenCanvas.setBackgroundResource(if (!isFixedCanvasMode) R.drawable.bg_settings_segment_active else R.drawable.bg_settings_segment_inactive)
            btnModeOpenCanvas.setTextColor(Color.parseColor(if (!isFixedCanvasMode) "#0F172A" else "#64748B"))
            btnModeOpenCanvas.setTypeface(null, if (!isFixedCanvasMode) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
        }
    }

    private fun showFlyoutAnimated(view: View) {
        if (view.visibility == View.VISIBLE) return
        view.animate().cancel()
        view.alpha = 0f
        view.scaleX = 0.95f
        view.scaleY = 0.95f
        view.visibility = View.VISIBLE
        view.animate()
            .alpha(1f)
            .scaleX(1f)
            .scaleY(1f)
            .setDuration(130)
            .setInterpolator(android.view.animation.DecelerateInterpolator())
            .start()
    }

    private fun hideFlyoutAnimated(view: View, onEnd: (() -> Unit)? = null) {
        if (view.visibility != View.VISIBLE) {
            onEnd?.invoke()
            return
        }
        view.animate().cancel()
        view.animate()
            .alpha(0f)
            .scaleX(0.95f)
            .scaleY(0.95f)
            .setDuration(100)
            .setInterpolator(android.view.animation.AccelerateInterpolator())
            .withEndAction {
                view.visibility = View.GONE
                view.alpha = 1f
                view.scaleX = 1f
                view.scaleY = 1f
                onEnd?.invoke()
            }
            .start()
    }

    private fun closeFlyouts(except: View? = null) {
        if (::flyoutPen.isInitialized && flyoutPen != except) hideFlyoutAnimated(flyoutPen)
        if (::flyoutEraser.isInitialized && flyoutEraser != except) {
            hideFlyoutAnimated(flyoutEraser)
            if (::cardEraserSizeSlider.isInitialized) cardEraserSizeSlider.visibility = View.GONE
        }
        if (::flyoutShapes.isInitialized && flyoutShapes != except) hideFlyoutAnimated(flyoutShapes)
        if (::flyoutBackground.isInitialized && flyoutBackground != except) {
            hideFlyoutAnimated(flyoutBackground)
            if (::cardColorGridSubPanel.isInitialized) cardColorGridSubPanel.visibility = View.GONE
        }
        if (::flyoutSystemFileMenu.isInitialized && flyoutSystemFileMenu != except) hideFlyoutAnimated(flyoutSystemFileMenu)
        if (::modalSettingsOverlay.isInitialized && modalSettingsOverlay != except) hideFlyoutAnimated(modalSettingsOverlay)
        if (::flyoutPageManager.isInitialized && flyoutPageManager != except && !isPageManagerPinned) {
            hideFlyoutAnimated(flyoutPageManager)
        }
    }

    private fun View.setSafeOnClickListener(debounceMs: Long = 200L, onSafeClick: (View) -> Unit) {
        var lastClickTime = 0L
        setOnClickListener { v ->
            val now = System.currentTimeMillis()
            if (now - lastClickTime >= debounceMs) {
                lastClickTime = now
                v.animate().scaleX(0.92f).scaleY(0.92f).setDuration(60).withEndAction {
                    v.animate().scaleX(1f).scaleY(1f).setDuration(70).start()
                }.start()
                onSafeClick(v)
            }
        }
    }

    private fun highlightColor(selectedView: View) {
        val views = listOf(colorBlack, colorWhite, colorBlue, colorRed)
        views.forEach { v ->
            v.scaleX = if (v == selectedView) 1.25f else 1.0f
            v.scaleY = if (v == selectedView) 1.25f else 1.0f
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            enableImmersiveMode()
        }
    }

    override fun onResume() {
        super.onResume()
        enableImmersiveMode()
    }

    private fun enableImmersiveMode() {
        try {
            // 1. AndroidX WindowCompat (Android 5.0 to 15+)
            WindowCompat.setDecorFitsSystemWindows(window, false)
            val controller = WindowCompat.getInsetsController(window, window.decorView)
            controller.systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            controller.hide(WindowInsetsCompat.Type.systemBars())

            // 2. Hardware / Smart Board Cutout display support (Android 9+)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                window.attributes.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }

            // 3. Keep Screen On (prevents screen dimming / sleeping on interactive whiteboards)
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

            // 4. Legacy System UI flags (dual-enforced for custom whiteboard ROMs / Android IFP TVs)
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LOW_PROFILE
            )
            @Suppress("DEPRECATION")
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        } catch (e: Throwable) {
            Log.e(TAG, "Error setting immersive mode", e)
        }
    }

    // ── EngineCallbacksInterface implementation ───────────────────────────────

    override fun onInvalidate() {}

    override fun onDocumentDirtyChanged(isDirty: Boolean) {
        Log.i(TAG, "Document dirty state: $isDirty")
    }

    override fun onRecoveryAvailable(snapshotPath: String, pageCount: Int, timestamp: Long) {
        Log.i(TAG, "Recovery available: $snapshotPath ($pageCount pages)")
    }

    override fun onAutoSaveComplete(success: Boolean) {
        Log.i(TAG, "AutoSave complete: $success")
    }

    private fun updatePageInfo() {
        val total = whiteboardSurface.getPageCount()
        val curr  = whiteboardSurface.getActivePageIndex() + 1
        runOnUiThread {
            tvPageInfo.text = "$curr / $total"
            btnPrevPage.isEnabled = (curr > 1)
            btnPrevPage.alpha = if (curr > 1) 1.0f else 0.4f
            btnNextPage.isEnabled = (curr < total)
            btnNextPage.alpha = if (curr < total) 1.0f else 0.4f

            if (::tvPageManagerCount.isInitialized) {
                tvPageManagerCount.text = "($curr/$total)"
            }
            if (::pageAdapter.isInitialized) {
                pageAdapter.updatePages(total, curr - 1)
            }
        }
    }

    override fun onPagesChanged() {
        updatePageInfo()
    }

    override fun onUndoRedoChanged(canUndo: Boolean, canRedo: Boolean) {
        runOnUiThread {
            btnUndo.alpha = if (canUndo) 1.0f else 0.4f
            btnRedo.alpha = if (canRedo) 1.0f else 0.4f
            btnUndo.isEnabled = canUndo
            btnRedo.isEnabled = canRedo
        }
    }

    override fun onError(message: String) {
        Log.e(TAG, "Engine error: $message")
    }

    private fun positionSelectionMenu(sRect: android.graphics.RectF) {
        val density = resources.displayMetrics.density

        // Get dimensions — use layout size if already measured, else force a measure pass
        var menuW = layoutSelectionContextMenu.width.toFloat()
        var menuH = layoutSelectionContextMenu.height.toFloat()
        if (menuW <= 0f || menuH <= 0f) {
            layoutSelectionContextMenu.measure(
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED)
            )
            menuW = layoutSelectionContextMenu.measuredWidth.toFloat().takeIf { it > 0f } ?: (340f * density)
            menuH = layoutSelectionContextMenu.measuredHeight.toFloat().takeIf { it > 0f } ?: (52f * density)
        }

        val screenW = whiteboardSurface.width.toFloat()
        val screenH = whiteboardSurface.height.toFloat()
        val gap     = 52f * density   // clearance above selection box for rotation handle
        val minEdge = 12f * density   // minimum distance from screen edges

        // Horizontal: center over selection, clamped to screen edges
        val rawX    = (sRect.left + sRect.right) / 2f - menuW / 2f
        val targetX = rawX.coerceIn(minEdge, (screenW - menuW - minEdge).coerceAtLeast(minEdge))

        // Vertical: prefer ABOVE the selection box (with room for rotation handle)
        val aboveY = sRect.top - menuH - gap
        val belowY = sRect.bottom + (16f * density)

        val targetY: Float = when {
            aboveY >= minEdge -> aboveY   // fits above with clearance from screen top
            else -> {
                // Not enough room above — flip below, clamp to avoid bottom toolbar
                val maxY = screenH - menuH - (120f * density)
                belowY.coerceIn(minEdge, maxY.coerceAtLeast(minEdge))
            }
        }

        layoutSelectionContextMenu.bringToFront()
        layoutSelectionContextMenu.x = targetX
        layoutSelectionContextMenu.y = targetY
    }

    override fun onSelectionChanged(hasSelection: Boolean, isLocked: Boolean, left: Float, top: Float, right: Float, bottom: Float) {
        runOnUiThread {
            if (hasSelection) {
                if (activeTool != ToolType.SELECTION) {
                    selectTool(ToolType.SELECTION, btnSelect)
                }
                layoutTapPastePopup.visibility = View.GONE

                val curSelColor = whiteboardSurface.getSelectedColor()
                viewSelectionColorDot.backgroundTintList = ColorStateList.valueOf(curSelColor)

                val hasClip = whiteboardSurface.hasClipboard()
                btnPasteSelection.isEnabled = hasClip
                btnPasteSelection.alpha = if (hasClip) 1.0f else 0.4f

                if (isLocked) {
                    btnLockSelection.setImageResource(R.drawable.ic_unlock)
                    btnColorSelection.visibility = View.GONE
                    btnCopySelection.visibility = View.GONE
                    btnPasteSelection.visibility = View.GONE
                    btnDuplicateSelection.visibility = View.GONE
                    btnFlipHSelection.visibility = View.GONE
                    btnFlipVSelection.visibility = View.GONE
                    btnAlignSelection.visibility = View.GONE
                    btnDeleteSelected.visibility = View.GONE
                    dividerLock1.visibility = View.GONE
                    dividerLock2.visibility = View.GONE
                    dividerLock3.visibility = View.GONE
                    dividerLock4.visibility = View.GONE
                    dividerLock5.visibility = View.GONE
                    dividerLock6.visibility = View.GONE
                    layoutAlignDropdown.visibility = View.GONE
                } else {
                    btnLockSelection.setImageResource(R.drawable.ic_lock)
                    btnColorSelection.visibility = View.VISIBLE
                    btnCopySelection.visibility = View.VISIBLE
                    btnPasteSelection.visibility = View.VISIBLE
                    btnDuplicateSelection.visibility = View.VISIBLE
                    btnFlipHSelection.visibility = View.VISIBLE
                    btnFlipVSelection.visibility = View.VISIBLE
                    btnAlignSelection.visibility = View.VISIBLE
                    btnDeleteSelected.visibility = View.VISIBLE
                    dividerLock1.visibility = View.VISIBLE
                    dividerLock2.visibility = View.VISIBLE
                    dividerLock3.visibility = View.VISIBLE
                    dividerLock4.visibility = View.VISIBLE
                    dividerLock5.visibility = View.VISIBLE
                    dividerLock6.visibility = View.VISIBLE
                }

                // Make the menu visible FIRST so it can be measured, then immediately position it.
                // positionSelectionMenu() calls measure() internally if width/height aren't known yet,
                // so there is no async GlobalLayoutListener race condition.
                layoutSelectionContextMenu.bringToFront()
                layoutSelectionContextMenu.visibility = View.VISIBLE
                val sRect = whiteboardSurface.canvasToScreenRect(left, top, right, bottom)
                positionSelectionMenu(sRect)

            } else {
                layoutSelectionContextMenu.visibility = View.GONE
                layoutAlignDropdown.visibility = View.GONE
            }
        }
    }

    fun getDefaultSlideDimensions(): Pair<Float, Float> {
        val defaultId = SlidePreset.getDefaultPresetId(this)
        if (defaultId == SlidePreset.PRESET_MATCH_SCREEN.id) {
            val sW = whiteboardSurface.width.takeIf { it > 0 } ?: resources.displayMetrics.widthPixels
            val sH = whiteboardSurface.height.takeIf { it > 0 } ?: resources.displayMetrics.heightPixels
            return Pair(sW.toFloat(), sH.toFloat())
        }
        val builtIn = SlidePreset.BUILT_IN_PRESETS.firstOrNull { it.id == defaultId }
        if (builtIn != null) return Pair(builtIn.width, builtIn.height)
        val custom = SlidePreset.getCustomPresets(this).firstOrNull { it.id == defaultId }
        if (custom != null) return Pair(custom.width, custom.height)
        return Pair(1920f, 1080f)
    }

    private fun setupSlidePresetsAndCustomSize() {
        val btnPresetMatchScreen: Button       = findViewById(R.id.btnPresetMatchScreen)
        val tvMatchScreenDesc: TextView?       = findViewById(R.id.tvMatchScreenDesc)
        val tvActivePresetSummary: TextView    = findViewById(R.id.tvActivePresetSummary)
        val btnApplyPresetCurrent: Button      = findViewById(R.id.btnApplyPresetCurrent)
        val btnApplyPresetAll: Button          = findViewById(R.id.btnApplyPresetAll)
        val btnSetPresetDefault: Button        = findViewById(R.id.btnSetPresetDefault)

        val btnAddCustomPreset: Button         = findViewById(R.id.btnAddCustomPreset)
        val llCustomPresetsContainer: LinearLayout = findViewById(R.id.llCustomPresetsContainer)
        val tvNoCustomPresets: TextView        = findViewById(R.id.tvNoCustomPresets)
        val btnSettingsReset: Button           = findViewById(R.id.btnSettingsReset)

        val modalCustomSlideSizeOverlay: FrameLayout = findViewById(R.id.modalCustomSlideSizeOverlay)
        val btnDlgClose: ImageButton           = findViewById(R.id.btnDlgClose)
        val etDlgPresetName: EditText          = findViewById(R.id.etDlgPresetName)
        val spinnerDlgTemplates: Spinner       = findViewById(R.id.spinnerDlgTemplates)
        val spinnerDlgUnit: Spinner            = findViewById(R.id.spinnerDlgUnit)
        val tvDlgWidthLabel: TextView?         = findViewById(R.id.tvDlgWidthLabel)
        val tvDlgHeightLabel: TextView?        = findViewById(R.id.tvDlgHeightLabel)
        val btnWidthMinus: ImageButton         = findViewById(R.id.btnWidthMinus)
        val btnWidthPlus: ImageButton          = findViewById(R.id.btnWidthPlus)
        val etDlgWidth: EditText               = findViewById(R.id.etDlgWidth)
        val btnHeightMinus: ImageButton        = findViewById(R.id.btnHeightMinus)
        val btnHeightPlus: ImageButton         = findViewById(R.id.btnHeightPlus)
        val etDlgHeight: EditText              = findViewById(R.id.etDlgHeight)
        val rgDlgOrientation: RadioGroup       = findViewById(R.id.rgDlgOrientation)
        val rbLandscape: RadioButton           = findViewById(R.id.rbLandscape)
        val rbPortrait: RadioButton            = findViewById(R.id.rbPortrait)
        val tvDlgRatioBadge: TextView          = findViewById(R.id.tvDlgRatioBadge)
        val btnDlgSavePreset: Button           = findViewById(R.id.btnDlgSavePreset)
        val btnDlgApplyCurrent: Button         = findViewById(R.id.btnDlgApplyCurrent)
        val btnDlgApplyAll: Button             = findViewById(R.id.btnDlgApplyAll)

        fun getEffectivePreset(preset: SlidePreset): SlidePreset {
            if (preset.id == SlidePreset.PRESET_MATCH_SCREEN.id) {
                val sW = whiteboardSurface.width.takeIf { it > 0 } ?: resources.displayMetrics.widthPixels
                val sH = whiteboardSurface.height.takeIf { it > 0 } ?: resources.displayMetrics.heightPixels
                return preset.copy(
                    width = sW.toFloat(),
                    height = sH.toFloat(),
                    unit = SlideUnit.PIXELS,
                    unitWidth = sW.toDouble(),
                    unitHeight = sH.toDouble()
                )
            }
            return preset
        }

        fun updatePresetUi() {
            val effPreset = getEffectivePreset(activeSlidePreset)
            val sW = whiteboardSurface.width.takeIf { it > 0 } ?: resources.displayMetrics.widthPixels
            val sH = whiteboardSurface.height.takeIf { it > 0 } ?: resources.displayMetrics.heightPixels
            tvMatchScreenDesc?.text = "Fit tablet screen ($sW × $sH px) with zero white margin"

            val isMatchScreen = (activeSlidePreset.id == SlidePreset.PRESET_MATCH_SCREEN.id)
            btnPresetMatchScreen.backgroundTintList = ColorStateList.valueOf(
                if (isMatchScreen) Color.parseColor("#10B981") else Color.parseColor("#0F172A")
            )
            btnPresetMatchScreen.text = if (isMatchScreen) "✓ Matched Screen" else "📱 Match Screen"

            tvActivePresetSummary.text = "Selected: ${activeSlidePreset.name} (${effPreset.width.toInt()} × ${effPreset.height.toInt()} px • ${effPreset.dimensionsDescription()} • ${effPreset.aspectRatioString()})"
        }

        fun applyPresetToPages(applyAll: Boolean) {
            val effPreset = getEffectivePreset(activeSlidePreset)
            if (applyAll) {
                whiteboardSurface.setAllPagesDimensions(effPreset.width, effPreset.height)
                pageAdapter.notifyDataSetChanged()
                pageAdapter.invalidateThumbnails()
                Toast.makeText(this, "Applied '${activeSlidePreset.name}' to all pages", Toast.LENGTH_SHORT).show()
            } else {
                val curPage = whiteboardSurface.getActivePageIndex()
                whiteboardSurface.setPageDimensions(curPage, effPreset.width, effPreset.height)
                pageAdapter.notifyItemChanged(curPage)
                pageAdapter.invalidateThumbnails()
                Toast.makeText(this, "Applied '${activeSlidePreset.name}' to slide ${curPage + 1}", Toast.LENGTH_SHORT).show()
            }
        }

        // Unified Presets List Management (Built-in Ratios + Custom Presets)
        fun refreshPresetsList() {
            llCustomPresetsContainer.removeAllViews()

            val allPresets = SlidePreset.getAllPresets(this)

            tvNoCustomPresets.visibility = if (allPresets.isEmpty()) View.VISIBLE else View.GONE

            allPresets.forEach { preset ->
                val itemView = layoutInflater.inflate(R.layout.item_custom_slide_preset, llCustomPresetsContainer, false)
                val tvName: TextView = itemView.findViewById(R.id.tvPresetName)
                val tvDetails: TextView = itemView.findViewById(R.id.tvPresetDetails)
                val btnApply: Button = itemView.findViewById(R.id.btnApplyPreset)
                val btnDelete: ImageButton = itemView.findViewById(R.id.btnDeletePreset)

                val effPreset = getEffectivePreset(preset)
                tvName.text = preset.name
                tvDetails.text = "${effPreset.dimensionsDescription()} • ${effPreset.aspectRatioString()} (${effPreset.orientation.name.lowercase().replaceFirstChar { it.uppercase() }})"

                val isSel = (preset.id == activeSlidePreset.id)
                itemView.setBackgroundResource(if (isSel) R.drawable.bg_settings_nav_active else R.drawable.bg_settings_nav_normal)

                // Every preset (ratios and custom sizes) has delete option available
                btnDelete.visibility = View.VISIBLE

                itemView.setOnClickListener {
                    activeSlidePreset = preset
                    updatePresetUi()
                    refreshPresetsList()
                }

                btnApply.setOnClickListener {
                    activeSlidePreset = preset
                    updatePresetUi()
                    applyPresetToPages(applyAll = true)
                    refreshPresetsList()
                }

                btnDelete.setOnClickListener {
                    val currentList = SlidePreset.getAllPresets(this@MainActivity)
                    currentList.removeAll { it.id == preset.id }
                    SlidePreset.saveAllPresets(this@MainActivity, currentList)
                    if (activeSlidePreset.id == preset.id) {
                        activeSlidePreset = currentList.firstOrNull() ?: SlidePreset.PRESET_MATCH_SCREEN
                        updatePresetUi()
                    }
                    refreshPresetsList()
                    Toast.makeText(this@MainActivity, "Deleted preset '${preset.name}'", Toast.LENGTH_SHORT).show()
                }

                llCustomPresetsContainer.addView(itemView)
            }
        }

        btnPresetMatchScreen.setOnClickListener {
            activeSlidePreset = SlidePreset.PRESET_MATCH_SCREEN
            updatePresetUi()
            refreshPresetsList()
        }

        btnApplyPresetCurrent.setOnClickListener {
            applyPresetToPages(applyAll = false)
        }

        btnApplyPresetAll.setOnClickListener {
            applyPresetToPages(applyAll = true)
        }

        btnSetPresetDefault.setOnClickListener {
            SlidePreset.setDefaultPresetId(this, activeSlidePreset.id)
            Toast.makeText(this, "'${activeSlidePreset.name}' set as default for new slides and imports", Toast.LENGTH_SHORT).show()
        }

        btnSettingsReset.setOnClickListener {
            SlidePreset.resetToDefaultPresets(this)
            activeSlidePreset = SlidePreset.PRESET_16_9
            SlidePreset.setDefaultPresetId(this, SlidePreset.PRESET_16_9.id)
            updatePresetUi()
            refreshPresetsList()
            Toast.makeText(this, "Presets and settings restored to defaults", Toast.LENGTH_SHORT).show()
        }

        refreshPresetsList()
        updatePresetUi()

        // ── Custom Slide Size Dialog Setup ─────────────────────────────────────
        val unitOptions = listOf(
            SlideUnit.RATIO,
            SlideUnit.INCHES,
            SlideUnit.CENTIMETERS,
            SlideUnit.MILLIMETERS,
            SlideUnit.PIXELS
        )
        val unitAdapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, unitOptions.map { it.label })
        spinnerDlgUnit.adapter = unitAdapter

        val templateOptions = listOf(
            "Custom",
            "16:9 Widescreen (Ratio 16:9)",
            "16:10 Display (Ratio 16:10)",
            "4:3 Standard (Ratio 4:3)",
            "3:2 Photo / Presentation (Ratio 3:2)",
            "21:9 UltraWide (Ratio 21:9)",
            "1:1 Square (Ratio 1:1)",
            "55.0 × 31.0 in (Large Presentation)",
            "13.333 × 7.500 in (1080p PPT)",
            "A4 Document (210 × 297 mm)",
            "US Letter (8.5 × 11.0 in)"
        )
        val templateAdapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, templateOptions)
        spinnerDlgTemplates.adapter = templateAdapter

        fun updateUnitLabels() {
            val curUnit = unitOptions.getOrElse(spinnerDlgUnit.selectedItemPosition) { SlideUnit.RATIO }
            if (curUnit == SlideUnit.RATIO) {
                tvDlgWidthLabel?.text = "Ratio Width (X):"
                tvDlgHeightLabel?.text = "Ratio Height (Y):"
            } else {
                tvDlgWidthLabel?.text = "Width (${curUnit.symbol}):"
                tvDlgHeightLabel?.text = "Height (${curUnit.symbol}):"
            }
        }

        fun updateDlgRatioPreview() {
            updateUnitLabels()
            val w = etDlgWidth.text.toString().toDoubleOrNull() ?: 16.0
            val h = etDlgHeight.text.toString().toDoubleOrNull() ?: 9.0
            val curUnit = unitOptions.getOrElse(spinnerDlgUnit.selectedItemPosition) { SlideUnit.RATIO }

            val pxW: Int
            val pxH: Int
            val ratio: Double

            if (curUnit == SlideUnit.RATIO) {
                val rX = w.coerceAtLeast(0.01)
                val rY = h.coerceAtLeast(0.01)
                ratio = rX / rY
                if (rbLandscape.isChecked || rX >= rY) {
                    pxW = 1920
                    pxH = (1920.0 * (rY / rX)).toInt().coerceAtLeast(100)
                } else {
                    pxH = 1920
                    pxW = (1920.0 * (rX / rY)).toInt().coerceAtLeast(100)
                }
            } else {
                pxW = (w * curUnit.pxFactor).toInt().coerceAtLeast(100)
                pxH = (h * curUnit.pxFactor).toInt().coerceAtLeast(100)
                ratio = if (pxH > 0) pxW.toDouble() / pxH else 1.0
            }

            tvDlgRatioBadge.text = String.format(Locale.US, "Ratio: %.2f:1 (%d × %d px)", ratio, pxW, pxH)
        }

        fun stepDimension(editText: EditText, delta: Double) {
            val cur = editText.text.toString().toDoubleOrNull() ?: 1.0
            val curUnit = unitOptions.getOrElse(spinnerDlgUnit.selectedItemPosition) { SlideUnit.RATIO }
            val step = when (curUnit) {
                SlideUnit.RATIO -> 1.0
                SlideUnit.INCHES -> 0.25
                SlideUnit.CENTIMETERS -> 0.50
                SlideUnit.MILLIMETERS -> 5.0
                SlideUnit.PIXELS -> 50.0
            }
            val next = (cur + (delta * step)).coerceAtLeast(0.1)
            editText.setText(if (curUnit == SlideUnit.PIXELS || (curUnit == SlideUnit.RATIO && next % 1.0 == 0.0)) next.toInt().toString() else String.format(Locale.US, "%.3f", next))
            updateDlgRatioPreview()
        }

        btnWidthMinus.setOnClickListener  { stepDimension(etDlgWidth, -1.0) }
        btnWidthPlus.setOnClickListener   { stepDimension(etDlgWidth, 1.0) }
        btnHeightMinus.setOnClickListener { stepDimension(etDlgHeight, -1.0) }
        btnHeightPlus.setOnClickListener  { stepDimension(etDlgHeight, 1.0) }

        etDlgWidth.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) { updateDlgRatioPreview() }
            override fun afterTextChanged(s: android.text.Editable?) {}
        })
        etDlgHeight.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) { updateDlgRatioPreview() }
            override fun afterTextChanged(s: android.text.Editable?) {}
        })

        spinnerDlgUnit.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                updateDlgRatioPreview()
            }
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        }

        spinnerDlgTemplates.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                when (position) {
                    1 -> { // 16:9
                        spinnerDlgUnit.setSelection(0) // RATIO
                        etDlgWidth.setText("16")
                        etDlgHeight.setText("9")
                        rbLandscape.isChecked = true
                    }
                    2 -> { // 16:10
                        spinnerDlgUnit.setSelection(0) // RATIO
                        etDlgWidth.setText("16")
                        etDlgHeight.setText("10")
                        rbLandscape.isChecked = true
                    }
                    3 -> { // 4:3
                        spinnerDlgUnit.setSelection(0) // RATIO
                        etDlgWidth.setText("4")
                        etDlgHeight.setText("3")
                        rbLandscape.isChecked = true
                    }
                    4 -> { // 3:2
                        spinnerDlgUnit.setSelection(0) // RATIO
                        etDlgWidth.setText("3")
                        etDlgHeight.setText("2")
                        rbLandscape.isChecked = true
                    }
                    5 -> { // 21:9
                        spinnerDlgUnit.setSelection(0) // RATIO
                        etDlgWidth.setText("21")
                        etDlgHeight.setText("9")
                        rbLandscape.isChecked = true
                    }
                    6 -> { // 1:1
                        spinnerDlgUnit.setSelection(0) // RATIO
                        etDlgWidth.setText("1")
                        etDlgHeight.setText("1")
                        rbLandscape.isChecked = true
                    }
                    7 -> { // 55x31 in
                        spinnerDlgUnit.setSelection(1) // INCHES
                        etDlgWidth.setText("55.000")
                        etDlgHeight.setText("30.988")
                        rbLandscape.isChecked = true
                    }
                    8 -> { // 13.333x7.5 in
                        spinnerDlgUnit.setSelection(1) // INCHES
                        etDlgWidth.setText("13.333")
                        etDlgHeight.setText("7.500")
                        rbLandscape.isChecked = true
                    }
                    9 -> { // A4
                        spinnerDlgUnit.setSelection(3) // mm
                        etDlgWidth.setText("210.0")
                        etDlgHeight.setText("297.0")
                        rbPortrait.isChecked = true
                    }
                    10 -> { // Letter
                        spinnerDlgUnit.setSelection(1) // INCHES
                        etDlgWidth.setText("8.500")
                        etDlgHeight.setText("11.000")
                        rbPortrait.isChecked = true
                    }
                }
                updateDlgRatioPreview()
            }
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        }

        rgDlgOrientation.setOnCheckedChangeListener { _, checkedId ->
            val w = etDlgWidth.text.toString().toDoubleOrNull() ?: 16.0
            val h = etDlgHeight.text.toString().toDoubleOrNull() ?: 9.0
            val curUnit = unitOptions.getOrElse(spinnerDlgUnit.selectedItemPosition) { SlideUnit.RATIO }
            val isLand = (checkedId == R.id.rbLandscape)
            if (isLand && h > w) {
                etDlgWidth.setText(if (curUnit == SlideUnit.RATIO && h % 1.0 == 0.0) h.toInt().toString() else String.format(Locale.US, "%.3f", h))
                etDlgHeight.setText(if (curUnit == SlideUnit.RATIO && w % 1.0 == 0.0) w.toInt().toString() else String.format(Locale.US, "%.3f", w))
            } else if (!isLand && w > h) {
                etDlgWidth.setText(if (curUnit == SlideUnit.RATIO && h % 1.0 == 0.0) h.toInt().toString() else String.format(Locale.US, "%.3f", h))
                etDlgHeight.setText(if (curUnit == SlideUnit.RATIO && w % 1.0 == 0.0) w.toInt().toString() else String.format(Locale.US, "%.3f", w))
            }
            updateDlgRatioPreview()
        }

        fun buildCustomPresetFromDialog(): SlidePreset {
            val name = etDlgPresetName.text.toString().trim()
            val w = etDlgWidth.text.toString().toDoubleOrNull() ?: 16.0
            val h = etDlgHeight.text.toString().toDoubleOrNull() ?: 9.0
            val curUnit = unitOptions.getOrElse(spinnerDlgUnit.selectedItemPosition) { SlideUnit.RATIO }
            val orient = if (rbLandscape.isChecked) SlideOrientation.LANDSCAPE else SlideOrientation.PORTRAIT
            return SlidePreset.createCustomPreset(name, w, h, curUnit, orient)
        }

        btnAddCustomPreset.setOnClickListener {
            etDlgPresetName.setText("")
            spinnerDlgTemplates.setSelection(0)
            spinnerDlgUnit.setSelection(0) // RATIO
            etDlgWidth.setText("16")
            etDlgHeight.setText("9")
            rbLandscape.isChecked = true
            updateDlgRatioPreview()
            modalCustomSlideSizeOverlay.visibility = View.VISIBLE
        }

        btnDlgClose.setOnClickListener {
            modalCustomSlideSizeOverlay.visibility = View.GONE
        }

        btnDlgSavePreset.setOnClickListener {
            val newPreset = buildCustomPresetFromDialog()
            val currentList = SlidePreset.getCustomPresets(this)
            currentList.add(newPreset)
            SlidePreset.saveCustomPresets(this, currentList)
            activeSlidePreset = newPreset
            updatePresetUi()
            refreshPresetsList()
            modalCustomSlideSizeOverlay.visibility = View.GONE
            Toast.makeText(this, "Saved preset '${newPreset.name}'", Toast.LENGTH_SHORT).show()
        }

        btnDlgApplyCurrent.setOnClickListener {
            val newPreset = buildCustomPresetFromDialog()
            activeSlidePreset = newPreset
            updatePresetUi()
            applyPresetToPages(applyAll = false)
            refreshPresetsList()
            modalCustomSlideSizeOverlay.visibility = View.GONE
        }

        btnDlgApplyAll.setOnClickListener {
            val newPreset = buildCustomPresetFromDialog()
            activeSlidePreset = newPreset
            updatePresetUi()
            applyPresetToPages(applyAll = true)
            refreshPresetsList()
            modalCustomSlideSizeOverlay.visibility = View.GONE
        }
    }

    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        if (ev.action == MotionEvent.ACTION_DOWN) {
            val x = ev.rawX.toInt()
            val y = ev.rawY.toInt()
            val tempRect = Rect()

            fun isInside(v: View?): Boolean {
                if (v == null || v.visibility != View.VISIBLE) return false
                v.getGlobalVisibleRect(tempRect)
                return tempRect.contains(x, y)
            }

            // 1. Settings Overlay
            if (::modalSettingsOverlay.isInitialized && modalSettingsOverlay.visibility == View.VISIBLE) {
                val card = modalSettingsOverlay.findViewById<View>(R.id.cardSettingsWindow)
                if (card != null && !isInside(card) && (::btnSettings.isInitialized && !isInside(btnSettings))) {
                    hideFlyoutAnimated(modalSettingsOverlay)
                }
            }

            // 2. Pen Flyout
            if (::flyoutPen.isInitialized && flyoutPen.visibility == View.VISIBLE) {
                if (!isInside(flyoutPen) && (::btnPen.isInitialized && !isInside(btnPen))) {
                    hideFlyoutAnimated(flyoutPen)
                }
            }

            // 3. Eraser Flyout
            if (::flyoutEraser.isInitialized && flyoutEraser.visibility == View.VISIBLE) {
                if (!isInside(flyoutEraser) && !isInside(if (::cardEraserSizeSlider.isInitialized) cardEraserSizeSlider else null) && (::btnEraser.isInitialized && !isInside(btnEraser))) {
                    hideFlyoutAnimated(flyoutEraser)
                    if (::cardEraserSizeSlider.isInitialized) cardEraserSizeSlider.visibility = View.GONE
                }
            }

            // 4. Shapes Flyout
            if (::flyoutShapes.isInitialized && flyoutShapes.visibility == View.VISIBLE) {
                if (!isInside(flyoutShapes) && (::btnShapes.isInitialized && !isInside(btnShapes))) {
                    hideFlyoutAnimated(flyoutShapes)
                }
            }

            // 5. Background Flyout
            if (::flyoutBackground.isInitialized && flyoutBackground.visibility == View.VISIBLE) {
                if (!isInside(flyoutBackground) && !isInside(if (::cardColorGridSubPanel.isInitialized) cardColorGridSubPanel else null) && (::btnBackground.isInitialized && !isInside(btnBackground))) {
                    hideFlyoutAnimated(flyoutBackground)
                    if (::cardColorGridSubPanel.isInitialized) cardColorGridSubPanel.visibility = View.GONE
                }
            }

            // 6. System File Menu
            if (::flyoutSystemFileMenu.isInitialized && flyoutSystemFileMenu.visibility == View.VISIBLE) {
                if (!isInside(flyoutSystemFileMenu) && (::btnFolder.isInitialized && !isInside(btnFolder))) {
                    hideFlyoutAnimated(flyoutSystemFileMenu)
                }
            }

            // 7. Page Manager Flyout (when not pinned)
            if (::flyoutPageManager.isInitialized && flyoutPageManager.visibility == View.VISIBLE && !isPageManagerPinned) {
                if (!isInside(flyoutPageManager) && (::tvPageInfo.isInitialized && !isInside(tvPageInfo))) {
                    hideFlyoutAnimated(flyoutPageManager)
                }
            }
        }
        return super.dispatchTouchEvent(ev)
    }
}
