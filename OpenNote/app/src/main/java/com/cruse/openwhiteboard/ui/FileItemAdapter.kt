package com.cruse.openwhiteboard.ui

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.cruse.openwhiteboard.R
import java.io.File

class FileItemAdapter(
    private var items: List<File>,
    private val onFolderClick: (File) -> Unit,
    private val onFileClick: (File) -> Unit
) : RecyclerView.Adapter<FileItemAdapter.ItemViewHolder>() {

    private var selectedFile: File? = null

    fun setSelectedFile(file: File?) {
        selectedFile = file
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ItemViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_folder_card, parent, false)
        return ItemViewHolder(view)
    }

    override fun onBindViewHolder(holder: ItemViewHolder, position: Int) {
        val file = items[position]
        holder.bind(file, file == selectedFile)
    }

    override fun getItemCount(): Int = items.size

    fun updateItems(newItems: List<File>) {
        items = newItems
        notifyDataSetChanged()
    }

    inner class ItemViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val ivIcon: ImageView = view.findViewById(R.id.ivFolderCardIcon)
        private val ivBadge: ImageView = view.findViewById(R.id.ivFileTypeBadge)
        private val tvName: TextView  = view.findViewById(R.id.tvFolderCardName)
        private val tvCount: TextView = view.findViewById(R.id.tvFolderCardCount)

        fun bind(file: File, isSelected: Boolean) {
            tvName.text = file.name
            itemView.setBackgroundResource(
                if (isSelected) R.drawable.bg_folder_card_selected
                else R.drawable.bg_folder_card
            )

            if (file.isDirectory) {
                ivIcon.scaleType = ImageView.ScaleType.CENTER_INSIDE
                ivIcon.setImageResource(R.drawable.ic_folder_modern)
                ivIcon.colorFilter = null
                ivBadge.visibility = View.GONE
                val count = FileUtils.folderChildCount(file)
                tvCount.text = if (count == 1) "1 item" else "$count items"
                itemView.setOnClickListener {
                    setSelectedFile(null)
                    onFolderClick(file)
                }
            } else {
                when {
                    FileUtils.isImageFile(file) -> {
                        ivIcon.colorFilter = null
                        ivBadge.visibility = View.GONE
                        ThumbnailLoader.loadThumbnail(file, ivIcon, R.drawable.ic_image, 160)
                    }
                    FileUtils.isPdfFile(file) -> {
                        ivIcon.colorFilter = null
                        ivBadge.visibility = View.VISIBLE
                        ivBadge.setImageResource(R.drawable.ic_pdf_badge)
                        ThumbnailLoader.loadThumbnail(file, ivIcon, R.drawable.ic_pdf_badge, 160)
                    }
                    FileUtils.isOwbFile(file) -> {
                        ivIcon.scaleType = ImageView.ScaleType.CENTER_INSIDE
                        ivIcon.setImageResource(R.drawable.ic_owb_badge)
                        ivIcon.colorFilter = null
                        ivBadge.visibility = View.GONE
                    }
                    else -> {
                        ivIcon.scaleType = ImageView.ScaleType.CENTER_INSIDE
                        ivIcon.setImageResource(R.drawable.ic_note)
                        ivIcon.setColorFilter(0xFF64748B.toInt())
                        ivBadge.visibility = View.GONE
                    }
                }
                tvCount.text = "${FileUtils.formatSize(file.length())} • ${FileUtils.formatDate(file)}"
                itemView.setOnClickListener {
                    setSelectedFile(file)
                    onFileClick(file)
                }
            }
        }
    }
}
