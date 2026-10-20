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
 * 主界面标题文字（可自定义）。
 * 默认：TexInject · 路漫漫其修远兮，吾将上下而求索
 */
object Brand {
    private const val PREF = "texinject_brand"
    private const val KEY_QUOTE = "quote"

    const val DEFAULT_QUOTE = "路漫漫其修远兮，吾将上下而求索"

    fun quote(c: Context): String =
        c.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .getString(KEY_QUOTE, DEFAULT_QUOTE) ?: DEFAULT_QUOTE

    fun setQuote(c: Context, v: String) {
        c.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit().putString(KEY_QUOTE, v).apply()
    }

    /** 标题 = "TexInject" + 分隔符 + 自定义诗句（空则只显示 TexInject）。 */
    fun title(c: Context): String {
        val q = quote(c).trim()
        return if (q.isEmpty()) "TexInject" else "TexInject · $q"
    }
}
