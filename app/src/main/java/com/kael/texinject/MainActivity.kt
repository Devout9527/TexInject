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

import android.app.Activity
import android.content.pm.ActivityInfo
import android.graphics.Color
import android.os.Bundle
import android.widget.FrameLayout
import com.kael.texinject.ui.OverlayLayout

/**
 * 桌面入口（启动器图标）：预览整个面板布局。
 *
 * 只渲染 UI 布局（previewMode=true）：不挂悬浮球、不挂任何悬浮窗/Overlay、
 * 不加载 native/音乐/脚本等任何注入功能。切分类只显示占位说明。
 * 默认横屏。
 */
class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        try {
            setContentView(OverlayLayout(this, true))
        } catch (e: Throwable) {
            setContentView(FrameLayout(this).apply {
                setBackgroundColor(Color.parseColor("#FF101012"))
            })
        }
    }
}