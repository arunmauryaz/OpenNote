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
import android.widget.LinearLayout
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import com.cruse.openwhiteboard.R

class ExportChoiceDialog : DialogFragment() {

    interface Callbacks {
        fun onWholeDocumentSelected()
        fun onCustomPagesSelected()
    }

    private var totalPages: Int = 1
    private var isShareMode: Boolean = false
    private var callbacks: Callbacks? = null

    companion object {
        fun newInstance(totalPages: Int, isShareMode: Boolean = false, callbacks: Callbacks): ExportChoiceDialog {
            val dialog = ExportChoiceDialog()
            dialog.totalPages = totalPages
            dialog.isShareMode = isShareMode
            dialog.callbacks = callbacks
            return dialog
        }

        fun newInstance(totalPages: Int, callbacks: Callbacks): ExportChoiceDialog {
            return newInstance(totalPages, false, callbacks)
        }
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
        dialog?.window?.requestFeature(Window.FEATURE_NO_TITLE)
        dialog?.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        return inflater.inflate(R.layout.dialog_export_pdf_choice, container, false)
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

        val tvTitle: TextView? = view.findViewById(R.id.tvExportChoiceTitle)
        val tvSubtitle: TextView = view.findViewById(R.id.tvExportChoiceSubtitle)
        val tvWholeDesc: TextView = view.findViewById(R.id.tvWholeDocDesc)
        val tvCustomPagesDesc: TextView? = view.findViewById(R.id.tvCustomPagesDesc)
        val btnWholeDoc: LinearLayout = view.findViewById(R.id.btnExportWholeDoc)
        val btnCustomPages: LinearLayout = view.findViewById(R.id.btnExportCustomPages)
        val btnClose: ImageButton = view.findViewById(R.id.btnExportChoiceClose)
        val btnCancel: Button = view.findViewById(R.id.btnExportChoiceCancel)

        if (isShareMode) {
            tvTitle?.text = "Share Whiteboard"
            tvSubtitle.text = "Document has $totalPages ${if (totalPages == 1) "page" else "pages"}. Choose a share range:"
            tvWholeDesc.text = "Share all $totalPages ${if (totalPages == 1) "page" else "pages"} as a multi-page PDF"
            tvCustomPagesDesc?.text = "Select or deselect specific slides/pages to share"
        } else {
            tvTitle?.text = "Export as PDF"
            tvSubtitle.text = "Document has $totalPages ${if (totalPages == 1) "page" else "pages"}. Choose an export range:"
            tvWholeDesc.text = "Export all $totalPages ${if (totalPages == 1) "page" else "pages"} into a single multi-page PDF"
            tvCustomPagesDesc?.text = "Select or deselect specific slides/pages to export"
        }

        btnWholeDoc.setOnClickListener {
            dismiss()
            callbacks?.onWholeDocumentSelected()
        }

        btnCustomPages.setOnClickListener {
            dismiss()
            callbacks?.onCustomPagesSelected()
        }

        btnClose.setOnClickListener { dismiss() }
        btnCancel.setOnClickListener { dismiss() }
    }
}
