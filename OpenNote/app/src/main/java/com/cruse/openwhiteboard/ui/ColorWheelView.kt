package com.cruse.openwhiteboard.ui

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/**
 * Modern Hexagonal Honeycomb Color Wheel / Matrix View.
 * Renders an authentic 91-cell hexagonal honeycomb spectrum matching
 * standard discrete web/RGB hex color mathematics (0, 3, 6, 9, C, F).
 */
class ColorWheelView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    data class HexCell(
        val q: Int,
        val r: Int,
        val rVal: Int, // 0..15
        val gVal: Int, // 0..15
        val bVal: Int, // 0..15
        val hexLabel: String,
        val baseColor: Int,
        var cx: Float = 0f,
        var cy: Float = 0f,
        val path: Path = Path()
    )

    private val cells = mutableListOf<HexCell>()
    private var selectedCell: HexCell? = null
    private var currentVal = 1f // Brightness multiplier (0..1)

    private val cellFillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }
    private val cellBorderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = Color.parseColor("#222226")
        strokeWidth = 1.2f
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }
    private val selectionOuterPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = Color.WHITE
        strokeWidth = 3.5f
    }
    private val selectionGlowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = Color.parseColor("#0A84FF")
        strokeWidth = 5.5f
    }

    private var pendingColor: Int? = null
    var onColorChangedListener: ((Int) -> Unit)? = null

    init {
        generateHexGrid()
    }

    /**
     * Builds the 91-cell radius-5 hexagonal grid with exact RGB coordinates
     */
    private fun generateHexGrid() {
        cells.clear()
        val radius = 5

        // Corner colors (q, r) in axial coordinates at distance 5
        // 0: Left (-5, 0) -> Red (15, 0, 0)
        // 1: Top-Left (-5, 5) -> Yellow (15, 15, 0)
        // 2: Top-Right (0, 5) -> Green (0, 15, 0)
        // 3: Right (5, 0) -> Cyan (0, 15, 15)
        // 4: Bottom-Right (5, -5) -> Blue (0, 0, 15)
        // 5: Bottom-Left (0, -5) -> Magenta (15, 0, 15)

        for (q in -radius..radius) {
            val r1 = max(-radius, -q - radius)
            val r2 = min(radius, -q + radius)
            for (r in r1..r2) {
                val (rVal, gVal, bVal, label) = computeCellRgb(q, r, radius)
                val color = Color.rgb(rVal * 17, gVal * 17, bVal * 17)
                cells.add(HexCell(q, r, rVal, gVal, bVal, label, color))
            }
        }

        // Default selected: Center cell (0, 0) = Black
        selectedCell = cells.find { it.q == 0 && it.r == 0 } ?: cells.firstOrNull()
    }

    private fun computeCellRgb(q: Int, r: Int, maxR: Int): Quad<Int, Int, Int, String> {
        if (q == 0 && r == 0) {
            return Quad(0, 0, 0, "000")
        }

        val s = -q - r
        val dist = maxOf(abs(q), abs(r), abs(s))
        if (dist == 0) return Quad(0, 0, 0, "000")

        // Angle in standard cartesian flat-top coordinates
        // Flat-top: X = 1.5 * q, Y = -sqrt(3) * (r + q/2)
        val fx = 1.5f * q
        val fy = -sqrt(3.0f) * (r + q / 2.0f)
        var angleDeg = (Math.toDegrees(atan2(fy.toDouble(), fx.toDouble())) + 360.0) % 360.0

        // Find sector (0..5)
        // Red = 180°, Yellow = 120°, Green = 60°, Cyan = 0°, Blue = 300°, Magenta = 240°
        // Align angle with 6 outer corners:
        // Sector 0: Red(180) to Yellow(120)
        // Sector 1: Yellow(120) to Green(60)
        // Sector 2: Green(60) to Cyan(0)
        // Sector 3: Cyan(360/0) to Blue(300)
        // Sector 4: Blue(300) to Magenta(240)
        // Sector 5: Magenta(240) to Red(180)

        val (rOut, gOut, bOut) = computeOuterRgb(q, r, dist)

        // Scale by distance (0..5) with discrete 3-step quantization
        val rStep = quantizeTo3(rOut * dist / maxR.toFloat())
        val gStep = quantizeTo3(gOut * dist / maxR.toFloat())
        val bStep = quantizeTo3(bOut * dist / maxR.toFloat())

        val hexStr = "${toHexDigit(rStep)}${toHexDigit(gStep)}${toHexDigit(bStep)}"
        return Quad(rStep, gStep, bStep, hexStr)
    }

    private fun computeOuterRgb(q: Int, r: Int, dist: Int): Triple<Int, Int, Int> {
        val s = -q - r
        // Project (q, r, s) to outer boundary (dist == 5)
        val scale = 5.0f / dist
        val nq = q * scale
        val nr = r * scale
        val ns = s * scale

        // Pure corners at radius 5:
        // Corner Red: q=-5, r=0, s=5
        // Corner Yellow: q=-5, r=5, s=0
        // Corner Green: q=0, r=5, s=-5
        // Corner Cyan: q=5, r=0, s=-5
        // Corner Blue: q=5, r=-5, s=0
        // Corner Magenta: q=0, r=-5, s=5

        return if (nq <= 0 && nr >= 0 && ns >= 0) {
            // Segment Red (-5,0,5) to Yellow (-5,5,0): R=15, G=0..15, B=0
            val t = (nr / 5.0f).coerceIn(0f, 1f)
            Triple(15, quantizeTo3(t * 15f), 0)
        } else if (nq <= 0 && nr >= 0 && ns <= 0) {
            // Segment Yellow (-5,5,0) to Green (0,5,-5): R=15..0, G=15, B=0
            val t = ((5.0f + nq) / 5.0f).coerceIn(0f, 1f)
            Triple(quantizeTo3((1f - t) * 15f), 15, 0)
        } else if (nq >= 0 && nr >= 0 && ns <= 0) {
            // Segment Green (0,5,-5) to Cyan (5,0,-5): R=0, G=15, B=0..15
            val t = (nq / 5.0f).coerceIn(0f, 1f)
            Triple(0, 15, quantizeTo3(t * 15f))
        } else if (nq >= 0 && nr <= 0 && ns <= 0) {
            // Segment Cyan (5,0,-5) to Blue (5,-5,0): R=0, G=15..0, B=15
            val t = (-nr / 5.0f).coerceIn(0f, 1f)
            Triple(0, quantizeTo3((1f - t) * 15f), 15)
        } else if (nq >= 0 && nr <= 0 && ns >= 0) {
            // Segment Blue (5,-5,0) to Magenta (0,-5,5): R=0..15, G=0, B=15
            val t = ((5.0f - nq) / 5.0f).coerceIn(0f, 1f)
            Triple(quantizeTo3(t * 15f), 0, 15)
        } else {
            // Segment Magenta (0,-5,5) to Red (-5,0,5): R=15, G=0, B=15..0
            val t = ((5.0f - ns) / 5.0f).coerceIn(0f, 1f)
            Triple(15, 0, quantizeTo3((1f - t) * 15f))
        }
    }

    private fun quantizeTo3(v: Float): Int {
        val raw = v.roundToInt().coerceIn(0, 15)
        // Map to nearest multiple of 3 (0, 3, 6, 9, 12=C, 15=F)
        val rem = raw % 3
        return if (rem == 1) raw - 1 else if (rem == 2) min(15, raw + 1) else raw
    }

    private fun toHexDigit(v: Int): Char {
        return when (v) {
            10 -> 'A'
            11 -> 'B'
            12 -> 'C'
            13 -> 'D'
            14 -> 'E'
            15 -> 'F'
            else -> '0' + v
        }
    }

    data class Quad<A, B, C, D>(val first: A, val second: B, val third: C, val fourth: D)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w <= 0 || h <= 0) return

        val centerX = w / 2f
        val centerY = h / 2f
        val maxRadiusGrid = 5

        // Flat-top hexagon side length s
        // Total grid width = 3 * maxRadiusGrid * s + 2 * s = (3 * 5 + 2) * s = 17 * s
        // Total grid height = sqrt(3) * (2 * maxRadiusGrid + 1) * s = sqrt(3) * 11 * s = ~19.05 * s
        val sideFromWidth = (w - 20f) / 17f
        val sideFromHeight = (h - 20f) / (sqrt(3.0f) * 11f)
        val s = min(sideFromWidth, sideFromHeight)

        textPaint.textSize = (s * 0.46f).coerceIn(7f, 12f)

        // Precompute geometry for all hexagon cells
        for (cell in cells) {
            // Flat-top pixel conversion
            cell.cx = centerX + s * 1.5f * cell.q
            cell.cy = centerY - s * sqrt(3.0f) * (cell.r + cell.q / 2.0f)

            // Construct 6-sided polygon path
            cell.path.reset()
            for (i in 0..5) {
                val angleDeg = 60f * i // 0, 60, 120, 180, 240, 300 for flat-top
                val angleRad = Math.toRadians(angleDeg.toDouble())
                val px = (cell.cx + s * cos(angleRad)).toFloat()
                val py = (cell.cy + s * sin(angleRad)).toFloat()
                if (i == 0) cell.path.moveTo(px, py) else cell.path.lineTo(px, py)
            }
            cell.path.close()
        }

        pendingColor?.let {
            setColor(it)
            pendingColor = null
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (cells.isEmpty()) return

        // 1. Draw all Hexagon Cells
        for (cell in cells) {
            val adjustedColor = applyBrightness(cell.baseColor, currentVal)
            cellFillPaint.color = adjustedColor
            canvas.drawPath(cell.path, cellFillPaint)
            canvas.drawPath(cell.path, cellBorderPaint)

            // Draw 3-digit Hex Label
            val lum = calculateLuminance(adjustedColor)
            textPaint.color = if (lum > 0.45f) Color.parseColor("#1C1C1E") else Color.parseColor("#F5F5F7")
            val textY = cell.cy - ((textPaint.descent() + textPaint.ascent()) / 2f)
            canvas.drawText(cell.hexLabel, cell.cx, textY, textPaint)
        }

        // 2. Draw Selected Cell Highlight Indicator
        selectedCell?.let { sel ->
            canvas.drawPath(sel.path, selectionGlowPaint)
            canvas.drawPath(sel.path, selectionOuterPaint)
        }
    }

    private fun applyBrightness(color: Int, brightness: Float): Int {
        val hsv = FloatArray(3)
        Color.colorToHSV(color, hsv)
        hsv[2] = (hsv[2] * brightness).coerceIn(0f, 1f)
        return Color.HSVToColor(hsv)
    }

    private fun calculateLuminance(color: Int): Float {
        val r = Color.red(color) / 255f
        val g = Color.green(color) / 255f
        val b = Color.blue(color) / 255f
        return 0.299f * r + 0.587f * g + 0.114f * b
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                selectClosestCell(event.x, event.y)
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private fun selectClosestCell(x: Float, y: Float) {
        var closest: HexCell? = null
        var minDist = Float.MAX_VALUE
        for (cell in cells) {
            val dist = hypot(x - cell.cx, y - cell.cy)
            if (dist < minDist) {
                minDist = dist
                closest = cell
            }
        }
        if (closest != null && closest != selectedCell) {
            selectedCell = closest
            invalidate()
            notifyColorChanged()
        }
    }

    fun setColor(color: Int) {
        if (cells.isEmpty() || width <= 0) {
            pendingColor = color
            return
        }

        // Find cell with closest RGB Euclidean distance
        val targetR = Color.red(color)
        val targetG = Color.green(color)
        val targetB = Color.blue(color)

        var bestCell = selectedCell
        var minDiff = Double.MAX_VALUE

        for (cell in cells) {
            val cr = cell.rVal * 17
            val cg = cell.gVal * 17
            val cb = cell.bVal * 17
            val diff = (targetR - cr).toDouble().pow(2.0) +
                       (targetG - cg).toDouble().pow(2.0) +
                       (targetB - cb).toDouble().pow(2.0)
            if (diff < minDiff) {
                minDiff = diff
                bestCell = cell
            }
        }

        selectedCell = bestCell
        invalidate()
    }

    fun setVal(value: Float) {
        currentVal = value.coerceIn(0f, 1f)
        invalidate()
        notifyColorChanged()
    }

    fun getSelectedColor(): Int {
        val base = selectedCell?.baseColor ?: Color.BLACK
        return applyBrightness(base, currentVal)
    }

    fun getSelectedHexCode(): String {
        val c = getSelectedColor()
        return String.format("#%06X", 0xFFFFFF and c)
    }

    private fun notifyColorChanged() {
        onColorChangedListener?.invoke(getSelectedColor())
    }
}

