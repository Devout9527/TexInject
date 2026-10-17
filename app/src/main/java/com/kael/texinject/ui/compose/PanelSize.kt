/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui.compose

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue

/**
 * 面板卡片尺寸（设置页可调）。
 *
 * 宽度会被 94% 屏宽兜底，高度会被 82% 屏高兜底，调太大也不会超出屏幕。
 */
object PanelSize {

    private const val PREF = "texinject_display"
    private const val KEY_W = "panel_w_dp"
    private const val KEY_H = "panel_h_dp"

    /** 宽度范围 / 默认值 */
    const val W_MIN = 320
    const val W_MAX = 760
    const val W_DEF = 540

    /** 高度（内容区）范围 / 默认值 */
    const val H_MIN = 160
    const val H_MAX = 560
    const val H_DEF = 260

    /**
     * 版本号。改动尺寸时自增，面板读到它会重组，实现「拖滑块即时生效」。
     */
    var revision by mutableIntStateOf(0)
        private set

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun widthDp(c: Context): Int =
        p(c).getInt(KEY_W, W_DEF).coerceIn(W_MIN, W_MAX)

    fun setWidthDp(c: Context, v: Int) {
        p(c).edit().putInt(KEY_W, v.coerceIn(W_MIN, W_MAX)).apply()
        revision++
    }

    fun heightDp(c: Context): Int =
        p(c).getInt(KEY_H, H_DEF).coerceIn(H_MIN, H_MAX)

    fun setHeightDp(c: Context, v: Int) {
        p(c).edit().putInt(KEY_H, v.coerceIn(H_MIN, H_MAX)).apply()
        revision++
    }

    fun resetWidth(c: Context) = setWidthDp(c, W_DEF)
    fun resetHeight(c: Context) = setHeightDp(c, H_DEF)
}
