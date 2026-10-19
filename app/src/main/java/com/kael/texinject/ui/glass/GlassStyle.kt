/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui.glass

import android.content.Context

/**
 * 界面玻璃风格。
 *
 * 0 = 无（纯半透明）
 * 1 = 高斯模糊
 * 2 = 液态玻璃（AGSL 折射 + 色散）
 */
object GlassStyle {

    const val NONE = 0
    const val BLUR = 1
    const val LIQUID = 2

    /**
     * 液态玻璃总开关。
     *
     * 已禁用：实测在游戏进程里 AGSL RuntimeShader(折射+高光) 会引发
     *   - TextureView + MediaPlayer 组合下的 SIGSEGV 崩溃
     *   - 主线程卡顿 / ANR
     * 实现(LiquidGlass.kt)完整保留，把这里改回 true 即可恢复。
     */
    const val LIQUID_GLASS_ENABLED = false

    private const val PREF = "texinject_display"
    private const val KEY = "glass_style"

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun get(c: Context): Int {
        val def = if (LIQUID_GLASS_ENABLED) LIQUID else NONE
        val v = p(c).getInt(KEY, def).coerceIn(NONE, LIQUID)
        // 禁用时：即使历史设置里存的是液态玻璃，也退回「无」
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

    /** 模糊半径（dp） */
    fun blurRadiusDp(c: Context): Int = p(c).getInt("glass_blur_dp", 18).coerceIn(0, 50)

    fun setBlurRadiusDp(c: Context, v: Int) {
        p(c).edit().putInt("glass_blur_dp", v.coerceIn(0, 50)).apply()
    }

    // ---- 液态玻璃可调参数 ----

    /** 折射带宽度 dp（越大，玻璃边缘越"厚"）。0 = 关闭折射 */
    fun refractionDp(c: Context): Int = p(c).getInt("lg_refraction", 56).coerceIn(0, 160)

    fun setRefractionDp(c: Context, v: Int) {
        p(c).edit().putInt("lg_refraction", v.coerceIn(0, 160)).apply()
    }

    /** 折射强度 dp（越大，边缘扭曲越明显） */
    fun amountDp(c: Context): Int = p(c).getInt("lg_amount", 48).coerceIn(0, 160)

    fun setAmountDp(c: Context, v: Int) {
        p(c).edit().putInt("lg_amount", v.coerceIn(0, 160)).apply()
    }

    /** 色散 x100（0 ~ 100）。越大，边缘彩边越明显 */
    fun dispersion(c: Context): Int = p(c).getInt("lg_dispersion", 35).coerceIn(0, 100)

    fun setDispersion(c: Context, v: Int) {
        p(c).edit().putInt("lg_dispersion", v.coerceIn(0, 100)).apply()
    }

    /** 边缘高光开关 */
    fun highlight(c: Context): Boolean = p(c).getBoolean("lg_highlight", true)

    fun setHighlight(c: Context, v: Boolean) {
        p(c).edit().putBoolean("lg_highlight", v).apply()
    }

    /** 高光锐利度 x10（1.0 = 最柔和，6.0 = 最锐利） */
    fun highlightFalloff(c: Context): Int = p(c).getInt("lg_hl_falloff", 10).coerceIn(5, 60)

    fun setHighlightFalloff(c: Context, v: Int) {
        p(c).edit().putInt("lg_hl_falloff", v.coerceIn(5, 60)).apply()
    }

    /** 液态玻璃强度倍率（0.4 ~ 2.0） */
    fun liquidStrength(c: Context): Float =
        p(c).getInt("glass_liquid_x10", 10).coerceIn(4, 20) / 10f

    fun setLiquidStrength(c: Context, v: Float) {
        p(c).edit().putInt("glass_liquid_x10", (v * 10).toInt().coerceIn(4, 20)).apply()
    }
}
