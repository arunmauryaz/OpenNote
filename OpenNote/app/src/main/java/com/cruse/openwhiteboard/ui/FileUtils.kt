package com.cruse.openwhiteboard.ui

import android.content.Context
import android.os.Build
import android.os.Environment
import android.os.StatFs
import java.io.File
import java.text.DecimalFormat
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object FileUtils {

    const val OWB_EXTENSION = ".owb"
    const val PDF_EXTENSION = ".pdf"

    fun getStorageRoots(ctx: Context): List<File> {
        val roots = mutableListOf<File>()
        @Suppress("DEPRECATION")
        val internal = Environment.getExternalStorageDirectory()
        if (internal != null && internal.exists()) roots.add(internal)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            val externalDirs = ctx.getExternalFilesDirs(null)
            for (dir in externalDirs) {
                if (dir == null) continue
                var d: File? = dir
                while (d != null && d.parentFile != null) {
                    val parent = d.parentFile!!
                    val parts = parent.absolutePath.split("/")
                    if (parent.absolutePath.startsWith("/storage/") && parts.size == 3) {
                        if (parent != internal && parent.exists()) roots.add(parent)
                        break
                    }
                    d = parent
                }
            }
        }
        return roots.distinctBy { it.absolutePath }
    }

    fun listDir(dir: File): List<File> {
        val entries = dir.listFiles()?.filter { !it.name.startsWith(".") } ?: emptyList()
        val folders = entries.filter { it.isDirectory }.sortedBy { it.name.lowercase() }
        val files   = entries.filter { it.isFile }.sortedBy { it.name.lowercase() }
        return folders + files
    }

    fun formatSize(bytes: Long): String {
        if (bytes < 0) return ""
        val kb = bytes / 1024.0
        val mb = kb / 1024.0
        val gb = mb / 1024.0
        val df = DecimalFormat("#.#")
        return when {
            gb >= 1 -> "${df.format(gb)} GB"
            mb >= 1 -> "${df.format(mb)} MB"
            kb >= 1 -> "${df.format(kb)} KB"
            else    -> "$bytes B"
        }
    }

    fun freeSpaceString(dir: File): String {
        return try {
            val stat = StatFs(dir.absolutePath)
            val free = stat.availableBlocksLong * stat.blockSizeLong
            formatSize(free) + " free"
        } catch (e: Exception) { "" }
    }

    fun formatDate(file: File): String {
        val sdf = SimpleDateFormat("dd MMM yyyy", Locale.getDefault())
        return sdf.format(Date(file.lastModified()))
    }

    fun folderChildCount(dir: File): Int = dir.listFiles()?.size ?: 0

    val IMAGE_EXTENSIONS = setOf(".png", ".jpg", ".jpeg", ".webp", ".bmp")

    fun isImageFile(f: File): Boolean {
        if (!f.isFile) return false
        val lower = f.name.lowercase()
        return IMAGE_EXTENSIONS.any { lower.endsWith(it) }
    }

    fun isOwbFile(f: File): Boolean =
        f.isFile && f.name.lowercase().endsWith(OWB_EXTENSION)

    fun isPdfFile(f: File): Boolean =
        f.isFile && f.name.lowercase().endsWith(PDF_EXTENSION)

    val SLIDE_EXTENSIONS = setOf(".pdf", ".pptx", ".ppt")

    fun isSlideFile(f: File): Boolean {
        if (!f.isFile) return false
        val lower = f.name.lowercase()
        return SLIDE_EXTENSIONS.any { lower.endsWith(it) }
    }

    fun buildOwbPath(folder: File, name: String): String {
        val safeName = name.trim().ifBlank { "Untitled" }
        val withExt = if (safeName.lowercase().endsWith(OWB_EXTENSION)) safeName
                      else "$safeName$OWB_EXTENSION"
        return File(folder, withExt).absolutePath
    }

    fun buildPdfPath(folder: File, name: String): String {
        val safeName = name.trim().ifBlank { "Exported" }
        val withExt = if (safeName.lowercase().endsWith(PDF_EXTENSION)) safeName
                      else "$safeName$PDF_EXTENSION"
        return File(folder, withExt).absolutePath
    }
}