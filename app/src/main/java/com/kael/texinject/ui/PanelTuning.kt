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

/**
 * 面板布局可调项（存在 texinject_panel）。
 *
 *  - padding   内容区内边距(dp)
 *  - headerH   表头高度(dp)，0 = 自适应
 *  - dividerH  分隔线厚度(dp)，0 = 隐藏
 *  - bodyH     主体高度(dp)，0 = 自适应（以左栏为准）
 */
object PanelTuning {
    private const val PREF = "texinject_panel"

    const val KEY_PADDING = "padding"
    const val KEY_HEADER_H = "header_h"
    const val KEY_DIVIDER_H = "divider_h"
    const val KEY_BODY_H = "body_h"
    const val KEY_ICON_DP = "icon_dp"

    const val DEF_PADDING = 10
    const val DEF_HEADER_H = 40
    const val DEF_DIVIDER_H = 1
    const val DEF_BODY_H = 260
    const val DEF_ICON_DP = 20

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun padding(c: Context) = p(c).getInt(KEY_PADDING, DEF_PADDING).coerceIn(0, 40)
    fun headerH(c: Context) = p(c).getInt(KEY_HEADER_H, DEF_HEADER_H).coerceIn(0, 120)
    fun dividerH(c: Context) = p(c).getInt(KEY_DIVIDER_H, DEF_DIVIDER_H).coerceIn(0, 12)
    fun bodyH(c: Context) = p(c).getInt(KEY_BODY_H, DEF_BODY_H).coerceIn(0, 900)
    fun iconDp(c: Context) = p(c).getInt(KEY_ICON_DP, DEF_ICON_DP).coerceIn(12, 48)

    fun set(c: Context, key: String, value: Int) {
        p(c).edit().putInt(key, value).apply()
    }
}
