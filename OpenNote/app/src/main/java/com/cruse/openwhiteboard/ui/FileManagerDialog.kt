package com.cruse.openwhiteboard.ui

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.DialogFragment
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.cruse.openwhiteboard.R
import java.io.File

class FileManagerDialog : DialogFragment() {

    interface Callbacks {
        fun onFmNewFile()
        fun onFmOpenFile(path: String)
        fun onFmImportSlides(path: String, importAsBackground: Boolean)
        fun onFmSaveFile(path: String)
        fun onFmExportPdf(path: String, pageIndices: List<Int>? = null)
        fun onFmSharePages(pageIndices: List<Int>)
        fun getWhiteboardSurface(): WhiteboardSurfaceView?
    }

    companion object {
        const val TAG = "FileManagerDialog"
        const val PREFS_NAME = "file_manager_prefs"
        const val PREF_RECENT = "recent_files"
        const val PREF_DEFAULT_IMPORT_MODE = "default_slide_import_mode"
        private const val MAX_RECENT = 5

        fun newInstance() = FileManagerDialog()
    }

    private var callbacks: Callbacks? = null
    private var actionMode = "open"
    private var currentDir: File? = null
    private val dirStack = ArrayDeque<File>()

    private lateinit var adapter: FileItemAdapter
    private lateinit var rvFiles: RecyclerView
    private lateinit var layoutBreadcrumb: LinearLayout
    private lateinit var layoutStorageCards: LinearLayout
    private lateinit var layoutEmptyState: LinearLayout
    private lateinit var tvEmptyMsg: TextView
    private lateinit var tvPathDisplay: TextView
    private lateinit var btnAction: Button
    private lateinit var btnSecondary: Button
    private lateinit var layoutRecentFiles: LinearLayout
    private lateinit var tvNoRecent: TextView
    private lateinit var tvSectionLabel: TextView

    // Import Mode Toggle (Background vs Object)
    private lateinit var layoutImportModeToggle: LinearLayout
    private lateinit var btnImportAsBg: TextView
    private lateinit var btnImportAsObject: TextView
    private var isImportAsBackground = true

    private var selectedFile: File? = null

    // Sidebar items for highlight
    private lateinit var btnFmNew: LinearLayout
    private lateinit var btnFmOpen: LinearLayout
    private lateinit var btnFmSave: LinearLayout
    private lateinit var btnFmSaveAs: LinearLayout
    private lateinit var btnFmExportPdf: LinearLayout
    private lateinit var btnFmShare: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setStyle(STYLE_NO_TITLE, 0)
    }

    override fun onStart() {
        super.onStart()
        dialog?.window?.apply {
            setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            setBackgroundDrawableResource(android.R.color.transparent)
        }
    }

    override fun onAttach(context: Context) {
        super.onAttach(context)
        callbacks = context as? Callbacks
    }

    override fun onDetach() {
        super.onDetach()
        callbacks = null
    }

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        val root = inflater.inflate(R.layout.dialog_file_manager, container, false)

        // Backdrop tap to dismiss
        root.findViewById<View>(R.id.fmBackdrop).setOnClickListener { dismiss() }
        root.findViewById<View>(R.id.fmWindowCard).setOnClickListener { /* Consume click inside card */ }

        rvFiles            = root.findViewById(R.id.rvFiles)
        layoutBreadcrumb   = root.findViewById(R.id.layoutBreadcrumb)
        layoutStorageCards = root.findViewById(R.id.layoutStorageCards)
        layoutEmptyState   = root.findViewById(R.id.layoutEmptyState)
        tvEmptyMsg         = root.findViewById(R.id.tvEmptyStateMsg)
        tvPathDisplay      = root.findViewById(R.id.tvCurrentPathDisplay)
        btnAction          = root.findViewById(R.id.btnFmAction)
        btnSecondary       = root.findViewById(R.id.btnFmSecondary)
        layoutRecentFiles  = root.findViewById(R.id.layoutRecentFiles)
        tvNoRecent         = root.findViewById(R.id.tvNoRecentFiles)
        tvSectionLabel     = root.findViewById(R.id.tvSectionLabel)

        layoutImportModeToggle = root.findViewById(R.id.layoutImportModeToggle)
        btnImportAsBg          = root.findViewById(R.id.btnImportAsBg)
        btnImportAsObject      = root.findViewById(R.id.btnImportAsObject)

        btnFmNew       = root.findViewById(R.id.btnFmNew)
        btnFmOpen      = root.findViewById(R.id.btnFmOpen)
        btnFmSave      = root.findViewById(R.id.btnFmSave)
        btnFmSaveAs    = root.findViewById(R.id.btnFmSaveAs)
        btnFmExportPdf = root.findViewById(R.id.btnFmExportPdf)
        btnFmShare     = root.findViewById(R.id.btnFmShare)

        val isCompact = resources.getBoolean(R.bool.is_compact_screen)
        val fmWindowCard = root.findViewById<View>(R.id.fmWindowCard)
        if (isCompact) {
            val lp = fmWindowCard.layoutParams as? FrameLayout.LayoutParams
            if (lp != null) {
                lp.width = ViewGroup.LayoutParams.MATCH_PARENT
                lp.height = ViewGroup.LayoutParams.MATCH_PARENT
                lp.gravity = android.view.Gravity.TOP or android.view.Gravity.START
                val m = resources.getDimensionPixelSize(R.dimen.dialog_file_manager_margin)
                val navBarPillOffset = (20 * resources.displayMetrics.density).toInt()
                lp.setMargins(m, m, m, m + navBarPillOffset)
                fmWindowCard.layoutParams = lp
            }

            val sidebarBtns = listOf(btnFmNew, btnFmOpen, btnFmSave, btnFmSaveAs, btnFmExportPdf, btnFmShare)
            sidebarBtns.forEach { btn ->
                btn.gravity = android.view.Gravity.CENTER
                btn.setPadding(0, 0, 0, 0)
                for (i in 0 until btn.childCount) {
                    val child = btn.getChildAt(i)
                    if (child is TextView) {
                        child.visibility = View.GONE
                    }
                }
            }
            root.findViewById<View>(R.id.tvRecentFilesHeader)?.visibility = View.GONE
            layoutRecentFiles.visibility = View.GONE
            tvNoRecent.visibility = View.GONE
        }

        // Read default slide import mode from SharedPreferences
        val prefs = requireContext().getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val defaultMode = prefs.getString(PREF_DEFAULT_IMPORT_MODE, "background") ?: "background"
        isImportAsBackground = (defaultMode == "background")
        updateImportModeToggleUi()

        btnImportAsBg.setOnClickListener {
            isImportAsBackground = true
            updateImportModeToggleUi()
        }

        btnImportAsObject.setOnClickListener {
            isImportAsBackground = false
            updateImportModeToggleUi()
        }

        setupRecyclerView()
        setupSidebarButtons(root)
        setupTopBarTools(root)
        setupStorageCards()
        populateRecentFiles()
        switchMode("open")
        checkPermission()

        return root
    }

    private fun setupRecyclerView() {
        adapter = FileItemAdapter(
            items = emptyList(),
            onFolderClick = { navigateInto(it) },
            onFileClick   = { onFileSelected(it) }
        )
        val isCompact = resources.getBoolean(R.bool.is_compact_screen)
        val screenWidthDp = resources.configuration.screenWidthDp
        val spanCount = when {
            screenWidthDp < 500 -> 2
            screenWidthDp < 840 -> if (isCompact) 4 else 3
            screenWidthDp < 1200 -> 4
            else -> 5
        }
        rvFiles.layoutManager = GridLayoutManager(requireContext(), spanCount)
        rvFiles.adapter = adapter
    }

    private fun setupSidebarButtons(root: View) {
        root.findViewById<ImageButton>(R.id.btnFmClose).setOnClickListener { dismiss() }

        btnFmNew.setOnClickListener {
            highlightSidebar(btnFmNew)
            AlertDialog.Builder(requireContext())
                .setTitle("Create new file")
                .setMessage("This will clear the current whiteboard canvas. Continue?")
                .setPositiveButton("New File") { _, _ ->
                    callbacks?.onFmNewFile()
                    dismiss()
                }
                .setNegativeButton("Cancel") { _, _ -> highlightSidebar(btnFmOpen) }
                .show()
        }

        btnFmOpen.setOnClickListener {
            switchMode("open")
        }

        btnFmSave.setOnClickListener {
            switchMode("save")
        }

        btnFmSaveAs.setOnClickListener {
            switchMode("saveas")
        }

        btnFmExportPdf.setOnClickListener {
            switchMode("export")
        }

        btnFmShare.setOnClickListener {
            highlightSidebar(btnFmShare)
            val surface = callbacks?.getWhiteboardSurface()
            val totalCount = surface?.getPageCount() ?: 1
            val choiceDialog = ExportChoiceDialog.newInstance(totalCount, isShareMode = true, object : ExportChoiceDialog.Callbacks {
                override fun onWholeDocumentSelected() {
                    callbacks?.onFmSharePages((0 until totalCount).toList())
                    dismiss()
                }

                override fun onCustomPagesSelected() {
                    if (surface != null) {
                        val pagesDialog = ExportPagesDialog.newInstance(surface, isShareMode = true, object : ExportPagesDialog.Callbacks {
                            override fun onExportPagesConfirmed(selectedPageIndices: List<Int>) {
                                callbacks?.onFmSharePages(selectedPageIndices)
                                dismiss()
                            }
                        })
                        pagesDialog.show(parentFragmentManager, "ExportPagesDialog")
                    } else {
                        callbacks?.onFmSharePages((0 until totalCount).toList())
                        dismiss()
                    }
                }
            })
            choiceDialog.show(parentFragmentManager, "ExportChoiceDialog")
        }

        btnSecondary.setOnClickListener {
            dismiss()
        }

        btnAction.setOnClickListener {
            handleActionClick()
        }
    }

    private fun handleActionClick() {
        val dir = currentDir
        when (actionMode) {
            "open" -> {
                val file = selectedFile
                if (file != null && file.exists()) {
                    if (FileUtils.isSlideFile(file)) {
                        callbacks?.onFmImportSlides(file.absolutePath, isImportAsBackground)
                    } else {
                        callbacks?.onFmOpenFile(file.absolutePath)
                    }
                    addToRecent(file.absolutePath)
                    dismiss()
                } else {
                    Toast.makeText(requireContext(), "Please select a file to open", Toast.LENGTH_SHORT).show()
                }
            }
            "save" -> {
                if (dir == null) {
                    Toast.makeText(requireContext(), "Select a folder first", Toast.LENGTH_SHORT).show()
                    return
                }
                showSaveNameDialog(dir) { path ->
                    callbacks?.onFmSaveFile(path)
                    addToRecent(path)
                    dismiss()
                }
            }
            "saveas" -> {
                if (dir == null) {
                    Toast.makeText(requireContext(), "Select a folder first", Toast.LENGTH_SHORT).show()
                    return
                }
                showSaveNameDialog(dir) { path ->
                    callbacks?.onFmSaveFile(path)
                    addToRecent(path)
                    dismiss()
                }
            }
            "export" -> {
                if (dir == null) {
                    Toast.makeText(requireContext(), "Select a folder first", Toast.LENGTH_SHORT).show()
                    return
                }
                showExportNameDialog(dir) { path ->
                    val surface = callbacks?.getWhiteboardSurface()
                    val totalCount = surface?.getPageCount() ?: 1
                    val choiceDialog = ExportChoiceDialog.newInstance(totalCount, object : ExportChoiceDialog.Callbacks {
                        override fun onWholeDocumentSelected() {
                            callbacks?.onFmExportPdf(path, (0 until totalCount).toList())
                            dismiss()
                        }

                        override fun onCustomPagesSelected() {
                            if (surface != null) {
                                val pagesDialog = ExportPagesDialog.newInstance(surface, object : ExportPagesDialog.Callbacks {
                                    override fun onExportPagesConfirmed(selectedPageIndices: List<Int>) {
                                        callbacks?.onFmExportPdf(path, selectedPageIndices)
                                        dismiss()
                                    }
                                })
                                pagesDialog.show(parentFragmentManager, "ExportPagesDialog")
                            } else {
                                callbacks?.onFmExportPdf(path, (0 until totalCount).toList())
                                dismiss()
                            }
                        }
                    })
                    choiceDialog.show(parentFragmentManager, "ExportChoiceDialog")
                }
            }
        }
    }

    private fun setupTopBarTools(root: View) {
        root.findViewById<ImageButton>(R.id.btnFmRefresh).setOnClickListener {
            currentDir?.let { loadDirectory(it) }
        }
        root.findViewById<ImageButton>(R.id.btnFmViewToggle).setOnClickListener {
            val gm = rvFiles.layoutManager as? GridLayoutManager
            val isCompact = resources.getBoolean(R.bool.is_compact_screen)
            val screenWidthDp = resources.configuration.screenWidthDp
            val defaultSpan = when {
                screenWidthDp < 500 -> 2
                screenWidthDp < 840 -> if (isCompact) 4 else 3
                screenWidthDp < 1200 -> 4
                else -> 5
            }
            if (gm?.spanCount != 1) {
                gm?.spanCount = 1
            } else {
                gm?.spanCount = defaultSpan
            }
            adapter.notifyDataSetChanged()
        }
        root.findViewById<ImageButton>(R.id.btnFmSearch).setOnClickListener {
            Toast.makeText(requireContext(), "Search files", Toast.LENGTH_SHORT).show()
        }
    }

    private fun switchMode(mode: String) {
        actionMode = mode
        when (mode) {
            "open"   -> highlightSidebar(btnFmOpen)
            "save"   -> highlightSidebar(btnFmSave)
            "saveas" -> highlightSidebar(btnFmSaveAs)
            "export" -> highlightSidebar(btnFmExportPdf)
        }
        updateActionButton()
    }

    private fun highlightSidebar(activeItem: LinearLayout) {
        val items = listOf(btnFmNew, btnFmOpen, btnFmSave, btnFmSaveAs, btnFmExportPdf, btnFmShare)
        items.forEach { item ->
            val isActive = (item == activeItem)
            item.setBackgroundResource(
                if (isActive) R.drawable.bg_sidebar_pill_active
                else android.R.color.transparent
            )
            for (i in 0 until item.childCount) {
                val child = item.getChildAt(i)
                if (child is TextView) {
                    child.setTextColor(if (isActive) 0xFF2563EB.toInt() else 0xFF475569.toInt())
                    child.setTypeface(null, if (isActive) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
                } else if (child is ImageView) {
                    child.setColorFilter(if (isActive) 0xFF2563EB.toInt() else 0xFF64748B.toInt())
                }
            }
        }
    }

    private fun updateActionButton() {
        btnAction.text = when (actionMode) {
            "open"   -> "Open"
            "save"   -> "Save"
            "saveas" -> "Save As"
            "export" -> "Export"
            else     -> "Select"
        }
    }

    // ─── Storage Cards (Local Drive / SD Card only) ─────────────────────────

    private fun setupStorageCards() {
        val roots = FileUtils.getStorageRoots(requireContext())
        layoutStorageCards.removeAllViews()

        if (roots.isEmpty()) {
            showEmptyState("No accessible storage found")
            return
        }

        val ctx = requireContext()
        val isCompact = resources.getBoolean(R.bool.is_compact_screen)
        val cardWidth = resources.getDimensionPixelSize(R.dimen.dialog_fm_storage_card_width)
        val cardHeight = resources.getDimensionPixelSize(R.dimen.dialog_fm_storage_card_height)
        val iconSize = resources.getDimensionPixelSize(R.dimen.dialog_fm_storage_card_icon_size)
        val cardMarginEnd = if (isCompact) dpToPx(6) else dpToPx(10)
        val cardPadH = if (isCompact) dpToPx(6) else dpToPx(10)
        val cardPadV = if (isCompact) dpToPx(3) else dpToPx(6)
        val titleSp = resources.getDimension(R.dimen.dialog_fm_storage_card_title_size) / resources.displayMetrics.scaledDensity
        val subSp = resources.getDimension(R.dimen.dialog_fm_storage_card_sub_size) / resources.displayMetrics.scaledDensity

        roots.forEachIndexed { index, root ->
            val isSelected = (index == 0)
            val card = LinearLayout(ctx).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.CENTER_VERTICAL
                layoutParams = LinearLayout.LayoutParams(cardWidth, cardHeight).apply {
                    marginEnd = cardMarginEnd
                }
                setBackgroundResource(
                    if (isSelected) R.drawable.bg_folder_card_selected
                    else R.drawable.bg_folder_card
                )
                setPadding(cardPadH, cardPadV, cardPadH, cardPadV)
                isClickable = true
                isFocusable = true
            }

            val iv = ImageView(ctx).apply {
                layoutParams = LinearLayout.LayoutParams(iconSize, iconSize)
                setImageResource(R.drawable.ic_drive)
                setColorFilter(if (isSelected) 0xFF2563EB.toInt() else 0xFF64748B.toInt())
            }

            val textLayout = LinearLayout(ctx).apply {
                orientation = LinearLayout.VERTICAL
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply { marginStart = if (isCompact) dpToPx(5) else dpToPx(8) }
            }

            val tvName = TextView(ctx).apply {
                text = if (index == 0) "Local drive" else "SD Card"
                textSize = titleSp
                setTypeface(null, android.graphics.Typeface.BOLD)
                setTextColor(if (isSelected) 0xFF2563EB.toInt() else 0xFF1E293B.toInt())
            }

            val tvFree = TextView(ctx).apply {
                text = FileUtils.freeSpaceString(root)
                textSize = subSp
                setTextColor(0xFF94A3B8.toInt())
            }

            textLayout.addView(tvName)
            textLayout.addView(tvFree)
            card.addView(iv)
            card.addView(textLayout)

            card.setOnClickListener {
                navigateTo(root)
                for (i in 0 until layoutStorageCards.childCount) {
                    val c = layoutStorageCards.getChildAt(i) as? LinearLayout ?: continue
                    val isCur = (c == card)
                    c.setBackgroundResource(
                        if (isCur) R.drawable.bg_folder_card_selected
                        else R.drawable.bg_folder_card
                    )
                    val icon = c.getChildAt(0) as? ImageView
                    icon?.setColorFilter(if (isCur) 0xFF2563EB.toInt() else 0xFF64748B.toInt())
                    val txtLayout = c.getChildAt(1) as? LinearLayout
                    val title = txtLayout?.getChildAt(0) as? TextView
                    title?.setTextColor(if (isCur) 0xFF2563EB.toInt() else 0xFF1E293B.toInt())
                }
            }
            layoutStorageCards.addView(card)
        }

        // Auto-select first storage
        if (layoutStorageCards.childCount > 0) {
            layoutStorageCards.getChildAt(0).performClick()
        }
    }

    private fun navigateTo(dir: File) {
        dirStack.clear()
        currentDir = dir
        loadDirectory(dir)
        rebuildBreadcrumb()
        tvPathDisplay.text = dir.absolutePath
        selectedFile = null
        layoutImportModeToggle.visibility = View.GONE
        updateActionButton()
        adapter.setSelectedFile(null)
    }

    private fun navigateInto(dir: File) {
        currentDir?.let { dirStack.addLast(it) }
        currentDir = dir
        loadDirectory(dir)
        rebuildBreadcrumb()
        tvPathDisplay.text = dir.absolutePath
        selectedFile = null
        layoutImportModeToggle.visibility = View.GONE
        updateActionButton()
        adapter.setSelectedFile(null)
    }

    private fun loadDirectory(dir: File) {
        val files = FileUtils.listDir(dir)
        adapter.updateItems(files)
        if (files.isEmpty()) {
            showEmptyState("This folder is empty")
        } else {
            hideEmptyState()
        }
        tvSectionLabel.text = if (files.any { it.isDirectory }) "Folders" else "Files"
    }

    private fun rebuildBreadcrumb() {
        layoutBreadcrumb.removeAllViews()
        val ctx = requireContext()
        val allDirs = dirStack.toList() + listOf(currentDir ?: return)

        allDirs.forEachIndexed { index, dir ->
            if (index > 0) {
                val sep = TextView(ctx).apply {
                    text = " › "
                    textSize = 12f
                    setTextColor(0xFF94A3B8.toInt())
                }
                layoutBreadcrumb.addView(sep)
            }
            val crumb = TextView(ctx).apply {
                val displayName = when {
                    index == 0 -> "Local drive"
                    dir.name.isBlank() -> dir.absolutePath
                    else -> dir.name
                }
                text = displayName
                textSize = 12f
                setPadding(dpToPx(6), dpToPx(4), dpToPx(6), dpToPx(4))
                val isLast = index == allDirs.size - 1
                setTextColor(if (isLast) 0xFF2563EB.toInt() else 0xFF475569.toInt())
                setTypeface(null, if (isLast) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
                if (!isLast) {
                    isClickable = true
                    isFocusable = true
                    val outValue = android.util.TypedValue()
                    ctx.theme.resolveAttribute(android.R.attr.selectableItemBackground, outValue, true)
                    setBackgroundResource(outValue.resourceId)
                    setOnClickListener {
                        while (dirStack.size > index) dirStack.removeLast()
                        currentDir = dir
                        loadDirectory(dir)
                        rebuildBreadcrumb()
                        tvPathDisplay.text = dir.absolutePath
                        selectedFile = null
                        adapter.setSelectedFile(null)
                    }
                }
            }
            layoutBreadcrumb.addView(crumb)
        }
    }

    private fun onFileSelected(file: File) {
        selectedFile = file
        tvPathDisplay.text = file.absolutePath
        if (actionMode == "open") {
            if (FileUtils.isSlideFile(file)) {
                layoutImportModeToggle.visibility = View.VISIBLE
                btnAction.text = "Import Slides"
            } else {
                layoutImportModeToggle.visibility = View.GONE
                callbacks?.onFmOpenFile(file.absolutePath)
                addToRecent(file.absolutePath)
                dismiss()
            }
        }
    }

    private fun updateImportModeToggleUi() {
        if (isImportAsBackground) {
            btnImportAsBg.setBackgroundResource(R.drawable.bg_pill_toggle_active)
            btnImportAsBg.setTextColor(0xFF2563EB.toInt())
            btnImportAsBg.setTypeface(null, android.graphics.Typeface.BOLD)

            btnImportAsObject.setBackgroundResource(android.R.color.transparent)
            btnImportAsObject.setTextColor(0xFF64748B.toInt())
            btnImportAsObject.setTypeface(null, android.graphics.Typeface.NORMAL)
        } else {
            btnImportAsObject.setBackgroundResource(R.drawable.bg_pill_toggle_active)
            btnImportAsObject.setTextColor(0xFF2563EB.toInt())
            btnImportAsObject.setTypeface(null, android.graphics.Typeface.BOLD)

            btnImportAsBg.setBackgroundResource(android.R.color.transparent)
            btnImportAsBg.setTextColor(0xFF64748B.toInt())
            btnImportAsBg.setTypeface(null, android.graphics.Typeface.NORMAL)
        }
    }

    private fun showSaveNameDialog(dir: File, onConfirm: (String) -> Unit) {
        val folderDisplayName = if (dir.name == "0" || dir.name.isBlank()) "Local drive" else dir.name
        val etName = EditText(requireContext()).apply {
            hint = "Untitled"
            setText(selectedFile?.nameWithoutExtension ?: "Untitled")
            setSingleLine(true)
        }
        AlertDialog.Builder(requireContext())
            .setTitle("Save Whiteboard")
            .setMessage("Save to: $folderDisplayName")
            .setView(etName)
            .setPositiveButton("Save") { _, _ ->
                val path = FileUtils.buildOwbPath(dir, etName.text.toString())
                onConfirm(path)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showExportNameDialog(dir: File, onConfirm: (String) -> Unit) {
        val folderDisplayName = if (dir.name == "0" || dir.name.isBlank()) "Local drive" else dir.name
        val etName = EditText(requireContext()).apply {
            hint = "Exported"
            setText("Whiteboard_Export")
            setSingleLine(true)
        }
        AlertDialog.Builder(requireContext())
            .setTitle("Export PDF")
            .setMessage("Export to: $folderDisplayName")
            .setView(etName)
            .setPositiveButton("Export") { _, _ ->
                val path = FileUtils.buildPdfPath(dir, etName.text.toString())
                onConfirm(path)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showEmptyState(msg: String) {
        layoutEmptyState.visibility = View.VISIBLE
        rvFiles.visibility = View.GONE
        tvEmptyMsg.text = msg
    }

    private fun hideEmptyState() {
        layoutEmptyState.visibility = View.GONE
        rvFiles.visibility = View.VISIBLE
    }

    // ─── Permission ──────────────────────────────────────────────────────────

    private fun checkPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                AlertDialog.Builder(requireContext())
                    .setTitle("Storage Access Required")
                    .setMessage("OpenWhiteBoard needs storage access to browse, open, and save files. Please grant 'All files access' in Settings.")
                    .setPositiveButton("Open Settings") { _, _ ->
                        try {
                            val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                            intent.data = Uri.parse("package:${requireContext().packageName}")
                            startActivity(intent)
                        } catch (e: Exception) {
                            startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
                        }
                    }
                    .setNegativeButton("Cancel") { _, _ -> dismiss() }
                    .show()
            }
        }
    }

    // ─── Recent Files ────────────────────────────────────────────────────────

    private fun addToRecent(path: String) {
        val prefs = requireContext().getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val existing = prefs.getString(PREF_RECENT, "")?.split("||")?.filter { it.isNotBlank() }
            ?.toMutableList() ?: mutableListOf()
        existing.remove(path)
        existing.add(0, path)
        val trimmed = existing.take(MAX_RECENT)
        prefs.edit().putString(PREF_RECENT, trimmed.joinToString("||")).apply()
    }

    private fun getRecentFiles(): List<String> {
        val prefs = requireContext().getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getString(PREF_RECENT, "")?.split("||")?.filter { it.isNotBlank() }
            ?: emptyList()
    }

    private fun populateRecentFiles() {
        val isCompact = resources.getBoolean(R.bool.is_compact_screen)
        if (isCompact) {
            tvNoRecent.visibility = View.GONE
            layoutRecentFiles.visibility = View.GONE
            return
        }
        val recents = getRecentFiles()
        layoutRecentFiles.removeAllViews()
        if (recents.isEmpty()) {
            tvNoRecent.visibility = View.VISIBLE
            return
        }
        tvNoRecent.visibility = View.GONE
        val ctx = requireContext()
        recents.take(5).forEach { path ->
            val file = File(path)
            val row = LinearLayout(ctx).apply {
                orientation = LinearLayout.HORIZONTAL
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dpToPx(32)
                ).apply { bottomMargin = dpToPx(2) }
                setPadding(dpToPx(4), 0, dpToPx(4), 0)
                gravity = android.view.Gravity.CENTER_VERTICAL
                isClickable = true
                isFocusable = true
                val typedValue = android.util.TypedValue()
                if (ctx.theme.resolveAttribute(android.R.attr.selectableItemBackground, typedValue, true)) {
                    setBackgroundResource(typedValue.resourceId)
                }
            }
            val iv = ImageView(ctx).apply {
                layoutParams = LinearLayout.LayoutParams(dpToPx(18), dpToPx(18))
                scaleType = ImageView.ScaleType.CENTER_CROP
                when {
                    FileUtils.isImageFile(file) -> {
                        colorFilter = null
                        ThumbnailLoader.loadThumbnail(file, this, R.drawable.ic_image, 64)
                    }
                    FileUtils.isPdfFile(file) -> {
                        colorFilter = null
                        ThumbnailLoader.loadThumbnail(file, this, R.drawable.ic_pdf_badge, 64)
                    }
                    FileUtils.isOwbFile(file) -> {
                        setImageResource(R.drawable.ic_owb_badge)
                        colorFilter = null
                    }
                    else -> {
                        setImageResource(R.drawable.ic_note)
                        setColorFilter(0xFF2563EB.toInt())
                    }
                }
            }
            val tv = TextView(ctx).apply {
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply { marginStart = dpToPx(6) }
                text = file.name
                textSize = 10.5f
                maxLines = 1
                ellipsize = android.text.TextUtils.TruncateAt.END
                setTextColor(0xFF4B5563.toInt())
            }
            row.addView(iv)
            row.addView(tv)
            row.setOnClickListener {
                if (file.exists()) {
                    callbacks?.onFmOpenFile(file.absolutePath)
                    addToRecent(file.absolutePath)
                    dismiss()
                } else {
                    Toast.makeText(ctx, "File no longer exists", Toast.LENGTH_SHORT).show()
                }
            }
            layoutRecentFiles.addView(row)
        }
    }

    private fun dpToPx(dp: Int): Int =
        (dp * resources.displayMetrics.density + 0.5f).toInt()
}