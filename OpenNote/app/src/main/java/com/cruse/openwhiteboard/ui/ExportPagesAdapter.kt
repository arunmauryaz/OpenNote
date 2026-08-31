package com.cruse.openwhiteboard.ui

import android.content.Context
import android.graphics.Bitmap
import android.util.LruCache
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import com.cruse.openwhiteboard.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class ExportPagesAdapter(
    private val context: Context,
    private val scope: CoroutineScope,
    private val whiteboardSurface: WhiteboardSurfaceView,
    private val totalPages: Int,
    private val onSelectionChanged: (selectedCount: Int, totalCount: Int) -> Unit
) : RecyclerView.Adapter<ExportPagesAdapter.PageViewHolder>() {

    val selectedIndices = mutableSetOf<Int>()

    private val thumbnailCache = object : LruCache<Int, Bitmap>(40) {
        override fun entryRemoved(evicted: Boolean, key: Int?, oldValue: Bitmap?, newValue: Bitmap?) {
            // let GC collect
        }
    }

    init {
        // By default, select all pages
        for (i in 0 until totalPages) {
            selectedIndices.add(i)
        }
    }

    fun selectAll() {
        for (i in 0 until totalPages) {
            selectedIndices.add(i)
        }
        notifyDataSetChanged()
        onSelectionChanged(selectedIndices.size, totalPages)
    }

    fun deselectAll() {
        selectedIndices.clear()
        notifyDataSetChanged()
        onSelectionChanged(0, totalPages)
    }

    fun getSelectedPagesList(): List<Int> {
        return selectedIndices.sorted()
    }

    override fun getItemCount(): Int = totalPages

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): PageViewHolder {
        val view = LayoutInflater.from(context).inflate(R.layout.item_export_page_card, parent, false)
        return PageViewHolder(view)
    }

    override fun onBindViewHolder(holder: PageViewHolder, position: Int) {
        val isSelected = selectedIndices.contains(position)

        holder.tvPageNum.text = "Page ${position + 1}"
        if (isSelected) {
            holder.cardRoot.background = ContextCompat.getDrawable(context, R.drawable.bg_page_card_active)
            holder.ivCheckBadge.setImageResource(R.drawable.ic_check_circle_filled)
            holder.tvStatus.text = "Included"
            holder.tvStatus.setTextColor(0xFF3DDC84.toInt())
        } else {
            holder.cardRoot.background = ContextCompat.getDrawable(context, R.drawable.bg_page_card)
            holder.ivCheckBadge.setImageResource(R.drawable.ic_check_circle_outline)
            holder.tvStatus.text = "Excluded"
            holder.tvStatus.setTextColor(0xFF8899A6.toInt())
        }

        holder.cardRoot.setOnClickListener {
            if (selectedIndices.contains(position)) {
                selectedIndices.remove(position)
            } else {
                selectedIndices.add(position)
            }
            notifyItemChanged(position)
            onSelectionChanged(selectedIndices.size, totalPages)
        }

        loadThumbnail(position, holder.ivThumb)
    }

    private fun loadThumbnail(pageIndex: Int, target: ImageView) {
        target.tag = pageIndex
        val cached = thumbnailCache.get(pageIndex)
        if (cached != null && !cached.isRecycled) {
            target.setImageBitmap(cached)
            return
        }

        target.setImageBitmap(null)
        scope.launch(Dispatchers.Default) {
            try {
                val tw = 320
                val th = 180
                val bmp = Bitmap.createBitmap(tw, th, Bitmap.Config.ARGB_8888)
                val ok = whiteboardSurface.renderPageThumbnail(pageIndex, bmp)
                if (ok) {
                    thumbnailCache.put(pageIndex, bmp)
                    withContext(Dispatchers.Main) {
                        if (target.tag == pageIndex) {
                            target.setImageBitmap(bmp)
                        }
                    }
                } else {
                    bmp.recycle()
                }
            } catch (_: Throwable) {}
        }
    }

    class PageViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val cardRoot: LinearLayout   = itemView.findViewById(R.id.cardExportPageRoot)
        val ivThumb: ImageView       = itemView.findViewById(R.id.ivExportPageThumb)
        val ivCheckBadge: ImageView  = itemView.findViewById(R.id.ivExportCheckBadge)
        val tvPageNum: TextView      = itemView.findViewById(R.id.tvExportPageNumber)
        val tvStatus: TextView       = itemView.findViewById(R.id.tvExportPageStatus)
    }
}
