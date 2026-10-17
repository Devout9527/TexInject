/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui

import android.app.Activity
import android.content.Context
import android.view.View
import android.widget.PopupWindow
import java.lang.ref.WeakReference
import java.util.Collections

/**
 * 防录屏：让【所有模块 UI】（主悬浮层 + 快捷键胶囊 + 网站浮窗 + 配置弹窗）
 * 不进入截屏 / 录屏，而【游戏画面照常保留】。
 *
 * 原理：SurfaceFlinger 图层级标记
 *
 *     SurfaceControl.Transaction.setFlags(layer, 0x40 | 0x80, 0x40 | 0x80)
 *         0x40 = eLayerSkipScreenshot   （Android 12+，截屏时跳过该图层）
 *         0x80 = eLayerSecure           （截屏与录屏都不出现该图层）
 *
 * 前提：每个模块 UI 必须拥有【自己的图层】，也就是必须住在【独立窗口】里：
 *   - 主悬浮层                  -> OverlayManager 里的独立 PopupWindow
 *   - 快捷键胶囊 / 网站浮窗 / 配置弹窗 -> 各自本来就是 PopupWindow
 *
 * 注意：不对游戏 Activity 窗口设置 FLAG_SECURE，
 *       否则整个游戏窗口（含游戏画面）会被一起隐藏。
 */
object AntiRecord {

    private const val PREF = "texinject_display"
    private const val KEY = "anti_record"

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun enabled(c: Context): Boolean = p(c).getBoolean(KEY, false)

    @Volatile
    private var lastActivity: Activity? = null

    /** 已登记的活动弹窗，用于开关切换时对「已经显示着的」窗口重新打标。 */
    private val livePopups = Collections.synchronizedList(mutableListOf<WeakReference<PopupWindow>>())

    fun remember(activity: Activity?) {
        lastActivity = activity
    }

    /** 切换开关：保存 + 立即作用于悬浮层与所有已显示的弹窗。 */
    fun setEnabled(c: Context, on: Boolean) {
        p(c).edit().putBoolean(KEY, on).apply()
        OverlayManager.refreshAntiRecord()
        synchronized(livePopups) {
            livePopups.removeAll { it.get() == null }
            livePopups.forEach { ref -> ref.get()?.let { applyToPopup(it, on) } }
        }
    }

    /** 主悬浮层（独立窗口）打标。 */
    fun refreshOverlay() {
        val ctx = lastActivity ?: return
        val ov = OverlayManager.overlayView() ?: return
        try {
            val ok = SkipScreenshot.apply(ov, enabled(ctx))
            com.kael.texinject.TexInjectHookInit.logExternal(
                "[TexInject/AntiRecord] overlay skip-screenshot=${enabled(ctx)} ok=$ok"
            )
        } catch (_: Throwable) {
        }
    }

    /**
     * 给一个模块 PopupWindow 打标（快捷键胶囊 / 网站浮窗 / 配置弹窗）。
     * 调用后该窗口会被登记，之后开关切换会自动同步。
     */
    fun securePopup(pw: PopupWindow, context: Context) {
        synchronized(livePopups) {
            livePopups.removeAll { it.get() == null }
            if (livePopups.none { it.get() === pw }) {
                livePopups.add(WeakReference(pw))
            }
        }
        applyToPopup(pw, enabled(context))
    }

    /** 对单个 PopupWindow 应用 / 撤销防录屏。 */
    fun applyToPopup(pw: PopupWindow, on: Boolean) {
        // 只做图层级 SKIP_SCREENSHOT(0x40)：该弹窗不进截屏，其余画面照常。
        // 不使用 FLAG_SECURE —— 那会把窗口/区域在截屏里打成黑色，甚至让整屏无法截屏。
        try {
            val cv: View? = pw.contentView
            if (cv != null) SkipScreenshot.apply(cv, on)
        } catch (_: Throwable) {
        }
    }
}
