package com.cruse.openwhiteboard

import android.graphics.Color
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.widget.LinearLayout
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class SlideStreamingImportTest {

    @get:Rule
    val activityRule = ActivityScenarioRule(MainActivity::class.java)

    @Test
    fun testProgressiveStreamingImport() {
        var localPdfFile: File? = null
        activityRule.scenario.onActivity { activity ->
            val targetPdf = File(activity.cacheDir, "test_deck_import_${System.currentTimeMillis()}.pdf")
            val doc = android.graphics.pdf.PdfDocument()
            val paint = android.graphics.Paint().apply {
                color = Color.DKGRAY
                textSize = 40f
            }
            for (i in 0 until 20) {
                val pageInfo = android.graphics.pdf.PdfDocument.PageInfo.Builder(1920, 1080, i + 1).create()
                val page = doc.startPage(pageInfo)
                val canvas = page.canvas
                canvas.drawColor(Color.WHITE)
                canvas.drawText("Slide ${i + 1} of 20", 120f, 150f, paint)
                doc.finishPage(page)
            }
            targetPdf.outputStream().buffered().use { doc.writeTo(it) }
            doc.close()

            localPdfFile = targetPdf
            // Trigger progressive streaming import of 20-page PDF
            activity.onFmImportSlides(targetPdf.absolutePath, true)
        }

        assertNotNull(localPdfFile)

        // Wait up to 5 seconds to allow Step 1 (Page 0 instant synchronous render) to complete
        var page0Ready = false
        val p0StartTime = System.currentTimeMillis()
        while (System.currentTimeMillis() - p0StartTime < 5000) {
            Thread.sleep(200)
            activityRule.scenario.onActivity {
                val surface = it.findViewById<com.cruse.openwhiteboard.ui.WhiteboardSurfaceView>(R.id.whiteboardSurface)
                if (surface.getPageBackgroundPath(0).isNotBlank()) {
                    page0Ready = true
                }
            }
            if (page0Ready) break
        }
        assertTrue("Page 0 must be ready within 5 seconds", page0Ready)

        activityRule.scenario.onActivity {
            val surface = it.findViewById<com.cruse.openwhiteboard.ui.WhiteboardSurfaceView>(R.id.whiteboardSurface)
            val streamBadge = it.findViewById<LinearLayout>(R.id.layoutStreamLoadingBadge)
            val pageCount = surface.getPageCount()

            // 1. All 20 page slots must be immediately allocated in the C++ engine
            assertEquals("Total pages should be 20", 20, pageCount)

            // 2. Active page must be 0
            assertEquals("Active page should be 0", 0, surface.getActivePageIndex())

            // 3. Page 0 texture/slide must be loaded
            val page0Path = surface.getPageBackgroundPath(0)
            assertTrue("Page 0 slide path must not be empty", page0Path.isNotBlank())
            assertTrue("Page 0 slide file must exist", File(page0Path).exists())

            // 4. Modal overlay should be dismissed (instant user control unlocked)
            val modalOverlay = it.findViewById<View>(R.id.modalImportProgressOverlay)
            assertEquals("Modal overlay must be dismissed for instant control", View.GONE, modalOverlay.visibility)

            // 5. User can draw immediately on page 0 without delay or freezing
            surface.setStrokeColor(Color.RED)
            surface.setStrokeWidth(5f)
            val now = SystemClock.uptimeMillis()
            surface.dispatchTouchEvent(MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, 100f, 100f, 0))
            surface.dispatchTouchEvent(MotionEvent.obtain(now, now + 10, MotionEvent.ACTION_MOVE, 200f, 200f, 0))
            surface.dispatchTouchEvent(MotionEvent.obtain(now, now + 20, MotionEvent.ACTION_UP, 200f, 200f, 0))
        }

        // Wait up to 25 seconds for all 20 pages to stream in background
        val startTime = System.currentTimeMillis()
        var allPagesLoaded = false

        while (System.currentTimeMillis() - startTime < 25000) {
            Thread.sleep(500)
            activityRule.scenario.onActivity {
                val surface = it.findViewById<com.cruse.openwhiteboard.ui.WhiteboardSurfaceView>(R.id.whiteboardSurface)
                var count = 0
                for (i in 0 until 20) {
                    if (surface.getPageBackgroundPath(i).isNotBlank()) {
                        count++
                    }
                }
                if (count == 20) {
                    allPagesLoaded = true
                }
            }
            if (allPagesLoaded) break
        }

        assertTrue("All 20 pages must finish background streaming", allPagesLoaded)

        // Verify completion state
        activityRule.scenario.onActivity {
            val surface = it.findViewById<com.cruse.openwhiteboard.ui.WhiteboardSurfaceView>(R.id.whiteboardSurface)
            val streamBadge = it.findViewById<LinearLayout>(R.id.layoutStreamLoadingBadge)

            // Stream badge should be hidden after completion
            assertEquals("Stream badge must be GONE after completion", View.GONE, streamBadge.visibility)

            // Active page was NOT interrupted and remained on page 0
            assertEquals("Active page must not be reset", 0, surface.getActivePageIndex())

            // User navigates to page 10
            surface.setActivePage(10)
            assertEquals("Active page should be 10", 10, surface.getActivePageIndex())
            val page10Path = surface.getPageBackgroundPath(10)
            assertTrue("Page 10 slide file must exist", File(page10Path).exists())
        }
    }
}
