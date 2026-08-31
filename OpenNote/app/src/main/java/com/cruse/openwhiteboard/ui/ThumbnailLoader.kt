package com.cruse.openwhiteboard.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.pdf.PdfRenderer
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.util.LruCache
import android.widget.ImageView
import java.io.File
import java.util.concurrent.Executors

object ThumbnailLoader {
    private val maxMemory = (Runtime.getRuntime().maxMemory() / 1024).toInt()
    private val cacheSize = maxMemory / 8 // 1/8th of available memory for thumbnails

    private val memoryCache = object : LruCache<String, Bitmap>(cacheSize) {
        override fun sizeOf(key: String, bitmap: Bitmap): Int {
            return bitmap.byteCount / 1024
        }
    }

    private val executor = Executors.newFixedThreadPool(3)
    private val mainHandler = Handler(Looper.getMainLooper())

    fun loadThumbnail(file: File, targetView: ImageView, defaultRes: Int, targetSize: Int = 160) {
        val path = file.absolutePath
        targetView.tag = path

        val cached = memoryCache.get(path)
        if (cached != null && !cached.isRecycled) {
            targetView.setImageBitmap(cached)
            targetView.scaleType = ImageView.ScaleType.CENTER_CROP
            return
        }

        targetView.setImageResource(defaultRes)
        targetView.scaleType = ImageView.ScaleType.CENTER_INSIDE

        executor.execute {
            try {
                val bitmap: Bitmap? = when {
                    FileUtils.isImageFile(file) -> decodeSampledBitmap(file, targetSize, targetSize)
                    FileUtils.isPdfFile(file)   -> renderPdfFirstPage(file, targetSize, targetSize)
                    else -> null
                }

                if (bitmap != null) {
                    memoryCache.put(path, bitmap)
                    mainHandler.post {
                        if (targetView.tag == path) {
                            targetView.setImageBitmap(bitmap)
                            targetView.scaleType = ImageView.ScaleType.CENTER_CROP
                        }
                    }
                }
            } catch (e: Throwable) {
                // Ignore decoding errors, fallback remains defaultRes
            }
        }
    }

    private fun decodeSampledBitmap(file: File, reqWidth: Int, reqHeight: Int): Bitmap? {
        return try {
            val options = BitmapFactory.Options().apply {
                inJustDecodeBounds = true
            }
            BitmapFactory.decodeFile(file.absolutePath, options)

            var inSampleSize = 1
            if (options.outHeight > reqHeight || options.outWidth > reqWidth) {
                val halfHeight = options.outHeight / 2
                val halfWidth = options.outWidth / 2
                while ((halfHeight / inSampleSize) >= reqHeight && (halfWidth / inSampleSize) >= reqWidth) {
                    inSampleSize *= 2
                }
            }

            options.inJustDecodeBounds = false
            options.inSampleSize = inSampleSize
            options.inPreferredConfig = Bitmap.Config.RGB_565 // Memory-efficient RGB_565 for thumbnails
            BitmapFactory.decodeFile(file.absolutePath, options)
        } catch (e: Throwable) {
            null
        }
    }

    private fun renderPdfFirstPage(file: File, reqWidth: Int, reqHeight: Int): Bitmap? {
        var pfd: ParcelFileDescriptor? = null
        var renderer: PdfRenderer? = null
        var page: PdfRenderer.Page? = null
        return try {
            pfd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
            renderer = PdfRenderer(pfd)
            if (renderer.pageCount > 0) {
                page = renderer.openPage(0)
                val w = page.width
                val h = page.height
                val scale = Math.min(reqWidth.toFloat() / w, reqHeight.toFloat() / h).coerceAtLeast(0.1f)
                val destW = (w * scale).toInt().coerceAtLeast(1)
                val destH = (h * scale).toInt().coerceAtLeast(1)

                val bmp = Bitmap.createBitmap(destW, destH, Bitmap.Config.ARGB_8888)
                val canvas = Canvas(bmp)
                canvas.drawColor(Color.WHITE)
                page.render(bmp, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
                bmp
            } else null
        } catch (e: Throwable) {
            null
        } finally {
            try { page?.close() } catch (e: Throwable) {}
            try { renderer?.close() } catch (e: Throwable) {}
            try { pfd?.close() } catch (e: Throwable) {}
        }
    }

    fun clearCache() {
        memoryCache.evictAll()
    }
}
