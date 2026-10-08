/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 * TexInject is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * TexInject is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with TexInject. If not, see <https://www.gnu.org/licenses/>.
 */
package com.kael.texinject.ui

import android.content.Context
import android.content.res.Configuration
import android.graphics.Color

/**
 * 主题色板（深/浅两套）。
 * 每个取色函数按 isDark 返回暗色/亮色两套值。
 */
object UiTheme {
    // 强调色
    val accentBlue = Color.parseColor("#0A84FF")
    val accentBlueDark = Color.parseColor("#0066CC")
    val red = Color.parseColor("#FF3B30")

    // MIUI 开关
    val switchOn = Color.parseColor("#0A84FF")
    val switchOff = Color.parseColor("#E5E7EB")
    val switchThumbShadow = Color.parseColor("#33000000")

    fun isDarkMode(context: Context): Boolean {
        val mode = context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        return mode == Configuration.UI_MODE_NIGHT_YES
    }

    // CreeperBox ClickGUI 配色
    val cbPanel = Color.parseColor("#C0252525")
    val cbList = Color.parseColor("#C0121212")
    val cbHighlight = Color.parseColor("#FF0062FF")
    val cbDivider = Color.parseColor("#A9A9A9")
    val cbCard = Color.parseColor("#D0121212")

    // 毛玻璃白（选项按钮底：半透明白，透出底下颜色 + 轻微模糊观感）
    val glassWhite = Color.parseColor("#59FFFFFF")
    val glassWhiteStrong = Color.parseColor("#73FFFFFF")
    val glassWhiteBorder = Color.parseColor("#66FFFFFF")
    val glassWhiteText = Color.parseColor("#FF141414")

    // 毛玻璃（半透明）色 —— 叠在模糊底衬上
    fun glassCard(dark: Boolean) = Color.parseColor(if (dark) "#B31E1E22" else "#B3F9FAFC")
    fun glassInner(dark: Boolean) = Color.parseColor(if (dark) "#732A2A2E" else "#73FFFFFF")
    fun glassButton(dark: Boolean) = Color.parseColor(if (dark) "#33FFFFFF" else "#26000000")
    fun glassButtonStroke(dark: Boolean) = Color.parseColor(if (dark) "#40FFFFFF" else "#33000000")
    val glassMuted = Color.parseColor("#59FFFFFF")
    val dim = Color.parseColor("#59000000")

    fun windowBg(dark: Boolean) = Color.parseColor(if (dark) "#F2121214" else "#E6000000")
    fun cardBg(dark: Boolean) = Color.parseColor(if (dark) "#1E1E22" else "#F9FAFC")
    fun innerCardBg(dark: Boolean) = Color.parseColor(if (dark) "#2A2A2E" else "#FFFFFF")
    fun sidebarBg(dark: Boolean) = Color.parseColor(if (dark) "#252529" else "#FFFFFF")
    fun divider(dark: Boolean) = Color.parseColor(if (dark) "#36363B" else "#E5E9F0")
    fun textPrimary(dark: Boolean) = Color.parseColor(if (dark) "#FFFFFF" else "#1C1C1E")
    fun textSecondary(dark: Boolean) = Color.parseColor(if (dark) "#9E9EA4" else "#8E8E93")
    fun inputBg(dark: Boolean) = Color.parseColor(if (dark) "#2A2A2E" else "#FFFFFF")
    fun inputBorder(dark: Boolean) = Color.parseColor(if (dark) "#3E3E44" else "#E5E7EB")
    fun exitBtnBg(dark: Boolean) = Color.parseColor(if (dark) "#3B1D1D" else "#FDE8E8")
    fun activeTabBg(dark: Boolean) = Color.parseColor(if (dark) "#1A3860" else "#EBF5FF")
}
