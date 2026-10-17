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

import android.app.Activity
import android.view.View
import com.kael.texinject.ui.compose.ComposeHostView
import com.kael.texinject.ui.compose.SettingsScreen

/**
 * 「设置」分类。
 *
 * 已迁移到 Compose：实际界面见 [com.kael.texinject.ui.compose.SettingsScreen]。
 * 这里只负责把它包成一个普通 View 交给面板（面板本身还是 View 体系，
 * 见 OverlayLayout），通过 [ComposeHostView] 挂载 Compose。
 */
object SettingsUi {
    fun buildView(activity: Activity): View =
        ComposeHostView(activity) { SettingsScreen(activity) }
}
