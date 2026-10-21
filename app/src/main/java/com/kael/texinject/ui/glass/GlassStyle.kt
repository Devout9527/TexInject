package com.kael.texinject.ui.glass

import android.content.Context

object GlassStyle {

    const val NONE = 0
    const val BLUR = 1
    const val LIQUID = 2

    const val LIQUID_GLASS_ENABLED = false

    private const val PREF = "texinject_display"
    private const val KEY = "glass_style"

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun get(c: Context): Int {
        val def = if (LIQUID_GLASS_ENABLED) LIQUID else NONE
        val v = p(c).getInt(KEY, def).coerceIn(NONE, LIQUID)
        return if (!LIQUID_GLASS_ENABLED && v == LIQUID) NONE else v
    }

    fun set(c: Context, v: Int) {
        val nv = if (!LIQUID_GLASS_ENABLED && v == LIQUID) NONE else v
        p(c).edit().putInt(KEY, nv.coerceIn(NONE, LIQUID)).apply()
    }

    fun name(v: Int): String = when (v) {
        NONE -> "无"
        BLUR -> "高斯模糊"
        else -> "液态玻璃"
    }

    fun blurRadiusDp(c: Context): Int = p(c).getInt("glass_blur_dp", 18).coerceIn(0, 50)

    fun setBlurRadiusDp(c: Context, v: Int) {
        p(c).edit().putInt("glass_blur_dp", v.coerceIn(0, 50)).apply()
    }

    fun refractionDp(c: Context): Int = p(c).getInt("lg_refraction", 56).coerceIn(0, 160)

    fun setRefractionDp(c: Context, v: Int) {
        p(c).edit().putInt("lg_refraction", v.coerceIn(0, 160)).apply()
    }

    fun amountDp(c: Context): Int = p(c).getInt("lg_amount", 48).coerceIn(0, 160)

    fun setAmountDp(c: Context, v: Int) {
        p(c).edit().putInt("lg_amount", v.coerceIn(0, 160)).apply()
    }

    fun dispersion(c: Context): Int = p(c).getInt("lg_dispersion", 35).coerceIn(0, 100)

    fun setDispersion(c: Context, v: Int) {
        p(c).edit().putInt("lg_dispersion", v.coerceIn(0, 100)).apply()
    }

    fun highlight(c: Context): Boolean = p(c).getBoolean("lg_highlight", true)

    fun setHighlight(c: Context, v: Boolean) {
        p(c).edit().putBoolean("lg_highlight", v).apply()
    }

    fun highlightFalloff(c: Context): Int = p(c).getInt("lg_hl_falloff", 10).coerceIn(5, 60)

    fun setHighlightFalloff(c: Context, v: Int) {
        p(c).edit().putInt("lg_hl_falloff", v.coerceIn(5, 60)).apply()
    }

    fun liquidStrength(c: Context): Float =
        p(c).getInt("glass_liquid_x10", 10).coerceIn(4, 20) / 10f

    fun setLiquidStrength(c: Context, v: Float) {
        p(c).edit().putInt("glass_liquid_x10", (v * 10).toInt().coerceIn(4, 20)).apply()
    }
}
