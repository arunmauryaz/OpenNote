package com.cruse.openwhiteboard.model

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

enum class SlideUnit(val symbol: String, val label: String, val pxFactor: Float) {
    INCHES("in", "Inches (in)", 144.0f),
    RATIO("ratio", "Aspect Ratio (X:Y)", 1.0f),
    CENTIMETERS("cm", "Centimeters (cm)", 56.6929f),
    MILLIMETERS("mm", "Millimeters (mm)", 5.66929f),
    PIXELS("px", "Pixels (px)", 1.0f);

    companion object {
        fun fromSymbol(sym: String): SlideUnit =
            values().firstOrNull { it.symbol.equals(sym, ignoreCase = true) } ?: INCHES
    }
}

enum class SlideOrientation {
    LANDSCAPE,
    PORTRAIT
}

data class SlidePreset(
    val id: String,
    val name: String,
    val width: Float,
    val height: Float,
    val unit: SlideUnit = SlideUnit.INCHES,
    val unitWidth: Double = (width / unit.pxFactor).toDouble(),
    val unitHeight: Double = (height / unit.pxFactor).toDouble(),
    val orientation: SlideOrientation = if (width >= height) SlideOrientation.LANDSCAPE else SlideOrientation.PORTRAIT,
    val isBuiltIn: Boolean = false
) {
    fun aspectRatioString(): String {
        val ratio = width / (if (height > 0) height else 1f)
        return String.format(java.util.Locale.US, "%.2f:1", ratio)
    }

    fun dimensionsDescription(): String {
        return when (unit) {
            SlideUnit.RATIO -> {
                val wStr = if (unitWidth % 1.0 == 0.0) unitWidth.toInt().toString() else String.format(java.util.Locale.US, "%.3f", unitWidth).trimEnd('0').trimEnd('.')
                val hStr = if (unitHeight % 1.0 == 0.0) unitHeight.toInt().toString() else String.format(java.util.Locale.US, "%.3f", unitHeight).trimEnd('0').trimEnd('.')
                "Ratio $wStr:$hStr (${width.toInt()} × ${height.toInt()} px)"
            }
            SlideUnit.INCHES -> String.format(java.util.Locale.US, "%.3f\" × %.3f\"", unitWidth, unitHeight)
            SlideUnit.CENTIMETERS -> String.format(java.util.Locale.US, "%.2f cm × %.2f cm", unitWidth, unitHeight)
            SlideUnit.MILLIMETERS -> String.format(java.util.Locale.US, "%.1f mm × %.1f mm", unitWidth, unitHeight)
            SlideUnit.PIXELS -> "${width.toInt()} × ${height.toInt()} px"
        }
    }

    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("id", id)
            put("name", name)
            put("width", width.toDouble())
            put("height", height.toDouble())
            put("unit", unit.symbol)
            put("unitWidth", unitWidth)
            put("unitHeight", unitHeight)
            put("orientation", orientation.name)
            put("isBuiltIn", isBuiltIn)
        }
    }

    companion object {
        const val PREFS_NAME = "slide_preset_prefs"
        const val KEY_CUSTOM_PRESETS = "custom_slide_presets"
        const val KEY_ACTIVE_PRESET_ID = "active_slide_preset_id"
        const val KEY_DEFAULT_PRESET_ID = "default_slide_preset_id"

        val PRESET_MATCH_SCREEN = SlidePreset(
            id = "preset_match_screen",
            name = "Match Screen (Zero White Margin)",
            width = 1920f,
            height = 1200f,
            unit = SlideUnit.PIXELS,
            isBuiltIn = true
        )

        val PRESET_16_9 = SlidePreset(
            id = "preset_16_9",
            name = "16:9 Widescreen",
            width = 1920f,
            height = 1080f,
            unit = SlideUnit.RATIO,
            unitWidth = 16.0,
            unitHeight = 9.0,
            isBuiltIn = true
        )

        val PRESET_16_10 = SlidePreset(
            id = "preset_16_10",
            name = "16:10 Display",
            width = 1920f,
            height = 1200f,
            unit = SlideUnit.RATIO,
            unitWidth = 16.0,
            unitHeight = 10.0,
            isBuiltIn = true
        )

        val PRESET_4_3 = SlidePreset(
            id = "preset_4_3",
            name = "4:3 Standard",
            width = 1440f,
            height = 1080f,
            unit = SlideUnit.RATIO,
            unitWidth = 4.0,
            unitHeight = 3.0,
            isBuiltIn = true
        )

        val PRESET_A4 = SlidePreset(
            id = "preset_a4",
            name = "A4 Document",
            width = 1414f,
            height = 2000f,
            unit = SlideUnit.MILLIMETERS,
            unitWidth = 210.0,
            unitHeight = 297.0,
            orientation = SlideOrientation.PORTRAIT,
            isBuiltIn = true
        )

        val PRESET_LETTER = SlidePreset(
            id = "preset_letter",
            name = "US Letter",
            width = 1545f,
            height = 2000f,
            unit = SlideUnit.INCHES,
            unitWidth = 8.500,
            unitHeight = 11.000,
            orientation = SlideOrientation.PORTRAIT,
            isBuiltIn = true
        )

        val BUILT_IN_PRESETS = listOf(
            PRESET_MATCH_SCREEN,
            PRESET_16_9,
            PRESET_16_10,
            PRESET_4_3,
            PRESET_A4,
            PRESET_LETTER
        )

        val STANDARD_RATIO_PRESETS = listOf(
            PRESET_16_9,
            PRESET_16_10,
            PRESET_4_3,
            PRESET_A4,
            PRESET_LETTER
        )

        fun fromJson(obj: JSONObject): SlidePreset {
            val unitSym = obj.optString("unit", "in")
            val unitEnum = SlideUnit.fromSymbol(unitSym)
            val w = obj.optDouble("width", 1920.0).toFloat()
            val h = obj.optDouble("height", 1080.0).toFloat()
            val uW = obj.optDouble("unitWidth", (w / unitEnum.pxFactor).toDouble())
            val uH = obj.optDouble("unitHeight", (h / unitEnum.pxFactor).toDouble())
            val orientStr = obj.optString("orientation", if (w >= h) "LANDSCAPE" else "PORTRAIT")
            val orientEnum = try { SlideOrientation.valueOf(orientStr) } catch (e: Exception) { SlideOrientation.LANDSCAPE }

            return SlidePreset(
                id = obj.optString("id", UUID.randomUUID().toString()),
                name = obj.optString("name", "Custom Slide"),
                width = w,
                height = h,
                unit = unitEnum,
                unitWidth = uW,
                unitHeight = uH,
                orientation = orientEnum,
                isBuiltIn = obj.optBoolean("isBuiltIn", false)
            )
        }

        const val KEY_ALL_PRESETS = "all_slide_presets"

        fun getAllPresets(context: Context): MutableList<SlidePreset> {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            val jsonStr = prefs.getString(KEY_ALL_PRESETS, null)
            if (jsonStr != null) {
                val list = mutableListOf<SlidePreset>()
                try {
                    val array = JSONArray(jsonStr)
                    for (i in 0 until array.length()) {
                        list.add(fromJson(array.getJSONObject(i)))
                    }
                } catch (e: Exception) {
                    e.printStackTrace()
                }
                return list
            }

            // Fallback / Initial default setup: load standard presets + legacy custom presets
            val initialList = mutableListOf<SlidePreset>()
            initialList.addAll(STANDARD_RATIO_PRESETS)
            val legacyJson = prefs.getString(KEY_CUSTOM_PRESETS, null)
            if (legacyJson != null) {
                try {
                    val array = JSONArray(legacyJson)
                    for (i in 0 until array.length()) {
                        initialList.add(fromJson(array.getJSONObject(i)))
                    }
                } catch (e: Exception) {
                    e.printStackTrace()
                }
            }
            saveAllPresets(context, initialList)
            return initialList
        }

        fun saveAllPresets(context: Context, presets: List<SlidePreset>) {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            val array = JSONArray()
            presets.forEach { array.put(it.toJson()) }
            prefs.edit().putString(KEY_ALL_PRESETS, array.toString()).apply()
        }

        fun resetToDefaultPresets(context: Context): List<SlidePreset> {
            val defaults = STANDARD_RATIO_PRESETS.toList()
            saveAllPresets(context, defaults)
            return defaults
        }

        fun getCustomPresets(context: Context): MutableList<SlidePreset> {
            return getAllPresets(context)
        }

        fun saveCustomPresets(context: Context, presets: List<SlidePreset>) {
            saveAllPresets(context, presets)
        }

        fun getDefaultPresetId(context: Context): String {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            return prefs.getString(KEY_DEFAULT_PRESET_ID, PRESET_16_9.id) ?: PRESET_16_9.id
        }

        fun setDefaultPresetId(context: Context, id: String) {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            prefs.edit().putString(KEY_DEFAULT_PRESET_ID, id).apply()
        }

        fun createCustomPreset(
            name: String,
            unitWidth: Double,
            unitHeight: Double,
            unit: SlideUnit,
            orientation: SlideOrientation
        ): SlidePreset {
            var rawW: Float
            var rawH: Float

            if (unit == SlideUnit.RATIO) {
                val rX = unitWidth.coerceAtLeast(0.01)
                val rY = unitHeight.coerceAtLeast(0.01)
                if (orientation == SlideOrientation.LANDSCAPE || rX >= rY) {
                    rawW = 1920f
                    rawH = (1920f * (rY / rX)).toFloat()
                } else {
                    rawH = 1920f
                    rawW = (1920f * (rX / rY)).toFloat()
                }
            } else {
                rawW = (unitWidth * unit.pxFactor).toFloat()
                rawH = (unitHeight * unit.pxFactor).toFloat()

                if (orientation == SlideOrientation.LANDSCAPE && rawH > rawW) {
                    val temp = rawW; rawW = rawH; rawH = temp
                } else if (orientation == SlideOrientation.PORTRAIT && rawW > rawH) {
                    val temp = rawW; rawW = rawH; rawH = temp
                }

                val maxDim = maxOf(rawW, rawH)
                val scale = if (maxDim > 0) 1920f / maxDim else 1f
                if (scale < 0.5f || scale > 2.0f) {
                    rawW *= scale
                    rawH *= scale
                }
            }

            val defaultName = if (unit == SlideUnit.RATIO) {
                val wStr = if (unitWidth % 1.0 == 0.0) unitWidth.toInt().toString() else String.format(java.util.Locale.US, "%.3f", unitWidth).trimEnd('0').trimEnd('.')
                val hStr = if (unitHeight % 1.0 == 0.0) unitHeight.toInt().toString() else String.format(java.util.Locale.US, "%.3f", unitHeight).trimEnd('0').trimEnd('.')
                "Custom $wStr:$hStr Ratio"
            } else {
                "Custom ${String.format(java.util.Locale.US, "%.1f×%.1f", unitWidth, unitHeight)} ${unit.symbol}"
            }

            return SlidePreset(
                id = "custom_" + System.currentTimeMillis(),
                name = name.ifBlank { defaultName },
                width = rawW,
                height = rawH,
                unit = unit,
                unitWidth = unitWidth,
                unitHeight = unitHeight,
                orientation = orientation,
                isBuiltIn = false
            )
        }
    }
}
