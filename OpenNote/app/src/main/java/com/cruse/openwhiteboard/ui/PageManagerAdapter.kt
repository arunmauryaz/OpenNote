package com.cruse.openwhiteboard.ui

import android.content.Context
import android.graphics.Bitmap
import android.util.LruCache
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.widget.PopupMenu
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import com.cruse.openwhiteboard.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class PageManagerAdapter(
    private val context: Context,
    private val scope: CoroutineScope,
    private val whiteboardSurface: WhiteboardSurfaceView,
    private val callbacks: Callbacks
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

    interface Callbacks {
        fun onPageSelected(index: Int)
        fun onAddPageRequested()
        fun onPageDuplicate(index: Int)
        fun onPageInsertBefore(index: Int)
        fun onPageInsertAfter(index: Int)
        fun onPageClear(index: Int)
        fun onPageDelete(index: Int)
        fun onPageReordered(fromIdx: Int, toIdx: Int)
        fun onSelectionCountChanged(count: Int)
    }

    companion object {
        private const val TYPE_PAGE = 0
        private const val TYPE_ADD  = 1
    }

    private var pageCount: Int = 1
    private var activePageIndex: Int = 0
    val selectedIndices = mutableSetOf<Int>()

    // Memory cache for 16:9 page thumbnails (320x180 RGB_565 or ARGB_8888)
    private val thumbnailCache = object : LruCache<Int, Bitmap>(128) {
        override fun entryRemoved(evicted: Boolean, key: Int?, oldValue: Bitmap?, newValue: Bitmap?) {
            // let GC recycle bitmap
        }
    }

    fun updatePages(count: Int, activeIdx: Int) {
        this.pageCount = count
        this.activePageIndex = activeIdx
        // Clean invalid selections
        selectedIndices.removeIf { it >= count }
        thumbnailCache.evictAll()
        notifyDataSetChanged()
        callbacks.onSelectionCountChanged(selectedIndices.size)
    }

    fun invalidateThumbnails() {
        thumbnailCache.evictAll()
        notifyDataSetChanged()
    }

    fun invalidateThumbnail(pageIndex: Int) {
        thumbnailCache.remove(pageIndex)
        notifyItemChanged(pageIndex)
    }

    fun selectAll() {
        selectedIndices.clear()
        for (i in 0 until pageCount) {
            selectedIndices.add(i)
        }
        notifyDataSetChanged()
        callbacks.onSelectionCountChanged(selectedIndices.size)
    }

    fun deselectAll() {
        selectedIndices.clear()
        notifyDataSetChanged()
        callbacks.onSelectionCountChanged(0)
    }

    fun clearSelection() {
        deselectAll()
    }

    fun invertSelection() {
        val newSet = mutableSetOf<Int>()
        for (i in 0 until pageCount) {
            if (!selectedIndices.contains(i)) {
                newSet.add(i)
            }
        }
        selectedIndices.clear()
        selectedIndices.addAll(newSet)
        notifyDataSetChanged()
        callbacks.onSelectionCountChanged(selectedIndices.size)
    }

    fun onItemMove(fromPosition: Int, toPosition: Int) {
        if (fromPosition == toPosition) return
        if (fromPosition >= pageCount || toPosition >= pageCount) return
        whiteboardSurface.reorderPage(fromPosition, toPosition)
        thumbnailCache.evictAll()
        notifyItemMoved(fromPosition, toPosition)
        callbacks.onPageReordered(fromPosition, toPosition)
    }

    override fun getItemCount(): Int = pageCount + 1 // trailing '+' card

    override fun getItemViewType(position: Int): Int {
        return if (position < pageCount) TYPE_PAGE else TYPE_ADD
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        return if (viewType == TYPE_PAGE) {
            val v = LayoutInflater.from(context).inflate(R.layout.item_page_card, parent, false)
            PageViewHolder(v)
        } else {
            val v = LayoutInflater.from(context).inflate(R.layout.item_page_add_card, parent, false)
            AddPageViewHolder(v)
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        if (holder is PageViewHolder) {
            val pageIndex = position
            val isCurrentActive = pageIndex == activePageIndex
            val isChecked = selectedIndices.contains(pageIndex)

            holder.tvPageNum.text = "${pageIndex + 1}"

            // Highlight border for active page
            if (isCurrentActive) {
                holder.cardRoot.background = ContextCompat.getDrawable(context, R.drawable.bg_page_card_active)
            } else {
                holder.cardRoot.background = ContextCompat.getDrawable(context, R.drawable.bg_page_card)
            }

            // Selection checkbox icon
            if (isChecked) {
                holder.ivSelectCheck.setImageResource(R.drawable.ic_check_circle_filled)
            } else {
                holder.ivSelectCheck.setImageResource(R.drawable.ic_check_circle_outline)
            }

            // Click card or thumbnail to switch page
            holder.cardRoot.setOnClickListener {
                callbacks.onPageSelected(pageIndex)
            }
            holder.ivThumb.setOnClickListener {
                callbacks.onPageSelected(pageIndex)
            }

            // Click selection icon to toggle batch select
            holder.ivSelectCheck.setOnClickListener {
                if (selectedIndices.contains(pageIndex)) {
                    selectedIndices.remove(pageIndex)
                } else {
                    selectedIndices.add(pageIndex)
                }
                notifyItemChanged(pageIndex)
                callbacks.onSelectionCountChanged(selectedIndices.size)
            }

            // 3-dot context menu
            holder.btnMore.setOnClickListener { v ->
                showPageContextMenu(v, pageIndex)
            }

            // Load thumbnail async
            loadPageThumbnail(pageIndex, holder.ivThumb)
        } else if (holder is AddPageViewHolder) {
            holder.cardRoot.setOnClickListener {
                callbacks.onAddPageRequested()
            }
        }
    }

    private fun showPageContextMenu(anchor: View, pageIndex: Int) {
        val popup = PopupMenu(context, anchor)
        popup.menu.add(0, 1, 0, "Duplicate Page")
        popup.menu.add(0, 2, 1, "Insert Page Before")
        popup.menu.add(0, 3, 2, "Insert Page After")
        popup.menu.add(0, 4, 3, "Clear Page")
        popup.menu.add(0, 5, 4, "Delete Page")

        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> callbacks.onPageDuplicate(pageIndex)
                2 -> callbacks.onPageInsertBefore(pageIndex)
                3 -> callbacks.onPageInsertAfter(pageIndex)
                4 -> callbacks.onPageClear(pageIndex)
                5 -> callbacks.onPageDelete(pageIndex)
            }
            true
        }
        popup.show()
    }

    private fun loadPageThumbnail(pageIndex: Int, targetView: ImageView) {
        targetView.tag = pageIndex
        val cached = thumbnailCache.get(pageIndex)
        if (cached != null && !cached.isRecycled) {
            targetView.setImageBitmap(cached)
            return
        }

        targetView.setImageBitmap(null)
        scope.launch(Dispatchers.Default) {
            try {
                val tw = 320
                val th = 180
                val bitmap = Bitmap.createBitmap(tw, th, Bitmap.Config.ARGB_8888)
                val ok = whiteboardSurface.renderPageThumbnail(pageIndex, bitmap)
                if (ok) {
                    thumbnailCache.put(pageIndex, bitmap)
                    withContext(Dispatchers.Main) {
                        if (targetView.tag == pageIndex) {
                            targetView.setImageBitmap(bitmap)
                        }
                    }
                } else {
                    bitmap.recycle()
                }
            } catch (e: Throwable) {
                // thumbnail failure fallback
            }
        }
    }

    class PageViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val cardRoot: LinearLayout   = itemView.findViewById(R.id.cardPageRoot)
        val ivThumb: ImageView       = itemView.findViewById(R.id.ivPageThumb)
        val tvPageNum: TextView      = itemView.findViewById(R.id.tvCardPageNumber)
        val ivSelectCheck: ImageView = itemView.findViewById(R.id.ivPageSelectCheck)
        val btnMore: ImageButton     = itemView.findViewById(R.id.btnPageCardMore)
    }

    class AddPageViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val cardRoot: LinearLayout = itemView.findViewById(R.id.cardAddPageRoot)
    }
}