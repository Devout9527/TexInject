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
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.ViewGroup

/**
 * 游戏内悬浮窗管理器（挂载到 decorView）。
 *
 * 做法：把 OverlayLayout 直接挂到游戏 Activity 的 decorView（同进程），
 * 不使用系统悬浮窗，因此不需要 SYSTEM_ALERT_WINDOW 权限。
 */
object OverlayManager {
    private const val TAG = "TEXINJECT_OVERLAY"
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var activeOverlay: OverlayLayout? = null

    fun attach(activity: Activity) {
        if (activity.isFinishing || activity.isDestroyed) return
        mainHandler.post {
            try {
                val decor = activity.window.decorView as? ViewGroup ?: return@post
                if (decor.findViewWithTag<View>(TAG) != null) return@post

                decor.clipChildren = false
                decor.clipToPadding = false
                // android.R.id.content
                decor.findViewById<ViewGroup>(android.R.id.content)?.let {
                    it.clipChildren = false
                    it.clipToPadding = false
                }

                val overlay = OverlayLayout(activity)
                overlay.tag = TAG
                decor.addView(
                    overlay,
                    ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT
                    )
                )
                overlay.bringToFront()
                activeOverlay = overlay
                com.kael.texinject.TexInjectHookInit.logExternal("[TexInject] overlay mounted to ${activity.javaClass.name}")
            } catch (e: Throwable) {
                com.kael.texinject.TexInjectHookInit.logExternal("[TexInject] attach failed: ${e.message}")
            }
        }
    }

    fun detach(activity: Activity) {
        mainHandler.post {
            try {
                val decor = activity.window.decorView as? ViewGroup ?: return@post
                decor.findViewWithTag<View>(TAG)?.let { decor.removeView(it) }
                activeOverlay = null
            } catch (e: Throwable) {}
        }
    }
}
