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
package com.kael.texinject

import android.content.Context

/** 启动次数统计（用于「启动满 100 次提示点 star」，只弹一次）。 */
object LaunchCounter {
    private const val PREF = "texinject_launch"
    private const val KEY_COUNT = "count"
    private const val KEY_STAR_SHOWN = "star_shown"

    const val STAR_THRESHOLD = 100

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    /** 记一次启动，返回累计次数。 */
    fun bump(c: Context): Int {
        val n = p(c).getInt(KEY_COUNT, 0) + 1
        p(c).edit().putInt(KEY_COUNT, n).apply()
        return n
    }

    fun count(c: Context): Int = p(c).getInt(KEY_COUNT, 0)

    fun starShown(c: Context): Boolean = p(c).getBoolean(KEY_STAR_SHOWN, false)

    fun markStarShown(c: Context) {
        p(c).edit().putBoolean(KEY_STAR_SHOWN, true).apply()
    }

    fun shouldAskStar(c: Context): Boolean = count(c) >= STAR_THRESHOLD && !starShown(c)
}
