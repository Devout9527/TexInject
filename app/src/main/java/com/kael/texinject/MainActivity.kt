/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject

import android.content.pm.ActivityInfo
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.kael.texinject.ui.compose.PanelOverlay
import com.kael.texinject.ui.compose.TexInjectTheme

/**
 * 桌面入口（启动器图标）：预览面板布局。默认横屏。
 *
 * 已迁移到 Compose 宿主（[ComponentActivity] + `setContent`）。
 * 面板已是 Compose 实现（[PanelOverlay]，previewMode=true 只渲染界面，
 * 不挂悬浮球、不加载任何注入功能）。
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        setContent {
            TexInjectTheme {
                PreviewScreen()
            }
        }
    }
}

@Composable
private fun PreviewScreen() {
    Box(
        Modifier
            .fillMaxSize()
            .background(Color(0xFF101012))
    ) {
        PanelOverlay(LocalContext.current as ComponentActivity, previewMode = true)
    }
}
