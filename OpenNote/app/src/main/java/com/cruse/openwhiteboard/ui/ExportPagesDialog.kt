package com.cruse.openwhiteboard.ui

import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.widget.Button
import android.widget.ImageButton
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.cruse.openwhiteboard.R

class ExportPagesDialog : DialogFragment() {

    interface Callbacks {
        fun onExportPagesConfirmed(selectedPageIndices: List<Int>)
    }

    private var whiteboardSurface: WhiteboardSurfaceView? = null
    private var callbacks: Callbacks? = null
    private var adapter: ExportPagesAdapter? = null
    private var isShareMode: Boolean = false

    companion object {
        fun newInstance(
            surfaceView: WhiteboardSurfaceView,
            isShareMode: Boolean = false,
            callback: Callbacks
        ): ExportPagesDialog {
            val dialog = ExportPagesDialog()
            dialog.whiteboardSurface = surfaceView
            dialog.isShareMode = isShareMode
            dialog.callbacks = callback
            return dialog
        }

        fun newInstance(
            surfaceView: WhiteboardSurfaceView,
            callback: Callbacks
        ): ExportPagesDialog {
            return newInstance(surfaceView, false, callback)
        }
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
        dialog?.window?.requestFeature(Window.FEATURE_NO_TITLE)
        dialog?.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        return inflater.inflate(R.layout.dialog_export_pdf_pages, container, false)
    }

    override fun onStart() {
        super.onStart()
        dialog?.window?.setLayout(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val surface = whiteboardSurface ?: return
        val totalCount = surface.getPageCount()

        val tvTitle: TextView? = view.findViewById(R.id.tvExportTitle)
        val tvBadge: TextView = view.findViewById(R.id.tvExportSelectionBadge)
        val btnSelectAll: Button = view.findViewById(R.id.btnExportSelectAll)
        val btnDeselectAll: Button = view.findViewById(R.id.btnExportDeselectAll)
        val btnClose: ImageButton = view.findViewById(R.id.btnExportClose)
        val btnCancel: Button = view.findViewById(R.id.btnExportCancel)
        val btnConfirm: Button = view.findViewById(R.id.btnExportConfirm)
        val tvFooter: TextView = view.findViewById(R.id.tvExportSummaryFooter)
        val rvPages: RecyclerView = view.findViewById(R.id.rvExportPages)

        if (isShareMode) {
            tvTitle?.text = "Select Pages to Share"
        } else {
            tvTitle?.text = "Select Pages to Export"
        }

        val screenWidthDp = resources.configuration.screenWidthDp
        val spanCount = when {
            screenWidthDp < 600 -> 2
            screenWidthDp < 900 -> 3
            screenWidthDp < 1200 -> 4
            else -> 5
        }
        rvPages.layoutManager = GridLayoutManager(requireContext(), spanCount)

        fun updateUi(selectedCount: Int, total: Int) {
            tvBadge.text = "$selectedCount of $total selected"
            val actionPrefix = if (isShareMode) "Share" else "Export"
            val actionVerb = if (isShareMode) "Sharing" else "Exporting"
            val actionPrompt = if (isShareMode) "share" else "export"

            if (selectedCount > 0) {
                btnConfirm.isEnabled = true
                btnConfirm.alpha = 1.0f
                btnConfirm.text = "$actionPrefix PDF ($selectedCount ${if (selectedCount == 1) "Page" else "Pages"})"
                tvFooter.text = "$actionVerb $selectedCount ${if (selectedCount == 1) "slide" else "slides"} into a multi-page PDF document"
            } else {
                btnConfirm.isEnabled = false
                btnConfirm.alpha = 0.5f
                btnConfirm.text = "$actionPrefix PDF (0 Pages)"
                tvFooter.text = "Please select at least 1 page to $actionPrompt"
            }
        }

        adapter = ExportPagesAdapter(
            context = requireContext(),
            scope = viewLifecycleOwner.lifecycleScope,
            whiteboardSurface = surface,
            totalPages = totalCount,
            onSelectionChanged = { selectedCount, total ->
                updateUi(selectedCount, total)
            }
        )

        rvPages.adapter = adapter
        updateUi(totalCount, totalCount)

        btnSelectAll.setOnClickListener {
            adapter?.selectAll()
        }

        btnDeselectAll.setOnClickListener {
            adapter?.deselectAll()
        }

        btnClose.setOnClickListener { dismiss() }
        btnCancel.setOnClickListener { dismiss() }

        btnConfirm.setOnClickListener {
            val selected = adapter?.getSelectedPagesList() ?: emptyList()
            if (selected.isNotEmpty()) {
                dismiss()
                callbacks?.onExportPagesConfirmed(selected)
            }
        }
    }
}
