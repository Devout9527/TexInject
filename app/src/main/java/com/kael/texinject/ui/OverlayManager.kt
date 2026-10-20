/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui

import android.app.Activity
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.ViewGroup
import android.widget.PopupWindow

/**
 * 游戏内悬浮层管理器。
 *
 * 【旧做法】把 OverlayLayout 直接加进游戏 Activity 的 decorView：
 *   好处是同窗口、触摸天然穿透；
 *   坏处是和游戏共用同一个 SurfaceFlinger 图层，无法单独做「防录屏」。
 *
 * 【现做法】把 OverlayLayout 放进一个独立的 PopupWindow：
 *   1. 独享一个图层 -> 可以用 SurfaceControl.setFlags(0x40|0x80) 单独跳过截屏/录屏，
 *      游戏画面（另一个图层）照常保留。
 *   2. 全屏窗口会吞掉所有触摸，所以用 ViewTreeObserver 的
 *      OnComputeInternalInsetsListener 声明「只有可见子视图所在区域可触摸」，
 *      其余区域的触摸回落到下层游戏窗口。
 *   3. PopupWindow 挂在 decorView 上，不需要 SYSTEM_ALERT_WINDOW 权限。
 */
object OverlayManager {
    private const val TAG = "TEXINJECT_OVERLAY"
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var activeOverlay: android.view.View? = null

    @Volatile
    private var activePopup: PopupWindow? = null

    /** 调试用：上次打印过的触摸区，避免每次重算都刷日志。 */
    @Volatile
    private var lastRegionLog: String? = null

    fun attach(activity: Activity) {
        if (activity.isFinishing || activity.isDestroyed) return
        // 临时排查开关：adb shell touch /data/local/tmp/texinject_no_overlay
        // 存在该文件时完全不挂悬浮层，用来判断闪退是否由独立窗口引起。
        if (java.io.File("/data/local/tmp/texinject_no_overlay").exists()) {
            logx("[TexInject] overlay skipped (flag /data/local/tmp/texinject_no_overlay)")
            return
        }
        mainHandler.post {
            try {
                if (activePopup != null) return@post

                // 分段诊断：/data/local/tmp/texinject_stage 里写数字 N，只执行到第 N 步。
                // 闪退时看最后一条 "[TexInject/Step] n"（没有对应 "n done"）就锁定是哪一步。
                val limit = stageLimit()
                fun stage(id: Int, name: String, block: () -> Unit) {
                    if (id > limit) {
                        logx("[TexInject/Step] $id $name SKIPPED(limit=$limit)")
                        return
                    }
                    logx("[TexInject/Step] $id $name")
                    try {
                        block()
                        logx("[TexInject/Step] $id done")
                    } catch (t: Throwable) {
                        logx("[TexInject/Step] $id FAILED: ${t.javaClass.simpleName}: ${t.message}")
                    }
                }

                // ---- 1. Compose 版面板（唯一内容）----
                var created: com.kael.texinject.ui.compose.ComposeHostView? = null
                stage(1, "create ComposeHostView(panel)") {
                    val ov = com.kael.texinject.ui.compose.ComposeHostView(activity) {
                        com.kael.texinject.ui.compose.PanelOverlay(activity, false)
                    }
                    ov.tag = TAG
                    // 可触摸区域 = 面板区域 ∪ 所有外部挂进来的 View（播放条/快捷键层）的屏幕矩形。
                    // 之前只给了面板区域，导致播放条点不动、拖不动（触摸全落到游戏里）。
                    ov.touchRegionProvider = { touchRegion(ov) }
                    created = ov
                }
                val overlay = created
                if (overlay == null) {
                    logx("[TexInject] attach aborted: ComposeHostView not created (stage=$limit)")
                    return@post
                }

                // ---- 2~5. 外层控件 ----
                stage(2, "NowPlayingBar") { NowPlayingBar.attach(activity, overlay) }
                stage(3, "NativeBall") {
                    com.kael.texinject.ui.NativeBall.attach(activity, overlay) {
                        com.kael.texinject.ui.PanelBridge.toggle?.invoke()
                    }
                }
                stage(4, "HotkeyLayer(MCP->native python)") { HotkeyLayer.attach(activity, overlay) }
                stage(5, "FeatureHotkeys") { FeatureHotkeys.init(activity) }

                // ---- 6. 独立全屏窗口 ----
                stage(6, "show PopupWindow") {
                    val pw = PopupWindow(
                        overlay,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        false
                    )
                    pw.isTouchable = true
                    pw.isFocusable = false          // 不抢焦点，游戏照跑
                    // FLAG_NOT_FOCUSABLE 会让窗口内控件拿不到输入焦点。
                    // INPUT_METHOD_NEEDED 会额外加 FLAG_ALT_FOCUSABLE_IM，
                    // 让输入法仍能弹出（原生 EditText 靠这个才有机会出键盘）。
                    pw.inputMethodMode = PopupWindow.INPUT_METHOD_NEEDED
                    pw.isOutsideTouchable = false
                    pw.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
                    pw.isClippingEnabled = false
                    pw.showAtLocation(
                        activity.window.decorView,
                        Gravity.TOP or Gravity.START, 0, 0
                    )
                    // 让 POPUP 窗口也带上 NOT_TOUCH_MODAL，配合 touchableRegion 做到穿透
                    PopupFlags.enableTouchPassthrough(pw)

                    activeOverlay = overlay
                    activePopup = pw
                }

                // ---- 7. 防录屏打图层标记 ----
                stage(7, "AntiRecord") {
                    AntiRecord.remember(activity)
                    AntiRecord.refreshOverlay()
                }

                logx(
                    "[TexInject] overlay mounted (independent window) to ${activity.javaClass.name}, " +
                        "passthrough=${overlay.passthroughOk}"
                )
            } catch (e: Throwable) {
                logx("[TexInject] attach failed: ${e.message}")
            }
        }
    }

    /** /data/local/tmp/texinject_stage 里的步数上限；文件不存在 = 全跑。 */
    private fun stageLimit(): Int {
        return try {
            val f = java.io.File("/data/local/tmp/texinject_stage")
            if (!f.exists()) Int.MAX_VALUE
            else f.readText().trim().toIntOrNull() ?: Int.MAX_VALUE
        } catch (_: Throwable) {
            Int.MAX_VALUE
        }
    }

    private fun logx(msg: String) {
        try {
            com.kael.texinject.TexInjectHookInit.logExternal(msg)
        } catch (_: Throwable) {
        }
    }

    /** 计算可触摸区域（屏幕坐标）。只算真正可见的子 View，避免整屏吞掉游戏触摸。 */
    private fun touchRegion(overlay: com.kael.texinject.ui.compose.ComposeHostView): android.graphics.Rect? {
        val out = android.graphics.Rect()
        var any = false
        com.kael.texinject.ui.compose.PanelTouchState.region?.let {
            if (!it.isEmpty) {
                out.set(it)
                any = true
            }
        }
        // 递归收集：全屏「承载层」（HotkeyLayer / WebsiteFloatingWindow 的容器）
        // 不能整块算进触摸区，否则整屏都被挡住、下面点不动。
        // 对这类层只取它内部真正可见的子 View。
        fun walk(v: android.view.View, o: android.graphics.Rect): Boolean {
            if (v.visibility != android.view.View.VISIBLE) return false
            if (v.width <= 0 || v.height <= 0) return false
            if (v is androidx.compose.ui.platform.ComposeView) return false
            val fullscreen = v.width >= overlay.width && v.height >= overlay.height
            if (fullscreen) {
                var got = false
                if (v is android.view.ViewGroup) {
                    for (i in 0 until v.childCount) {
                        if (walk(v.getChildAt(i), o)) got = true
                    }
                }
                return got
            }
            val r = android.graphics.Rect()
            if (!v.getGlobalVisibleRect(r) || r.isEmpty) return false
            o.union(r)
            return true
        }
        for (i in 0 until overlay.childCount) {
            if (walk(overlay.getChildAt(i), out)) any = true
        }
        // 临时诊断：只在并集变化时打印，避免刷屏。走模块 logger，能进 LSPosed 日志。
        try {
            val changed = lastRegionLog != out.toString()
            if (changed) {
                lastRegionLog = out.toString()
                com.kael.texinject.TexInjectHookInit.logExternal("[TexInject/Region] union=$out")
                for (i in 0 until overlay.childCount) {
                    val c = overlay.getChildAt(i)
                    val rr = android.graphics.Rect()
                    val vis = c.getGlobalVisibleRect(rr)
                    com.kael.texinject.TexInjectHookInit.logExternal(
                        "[TexInject/Region] child[$i] ${c.javaClass.simpleName} " +
                        "vis=${c.visibility} wh=${c.width}x${c.height} rect=$rr ok=$vis"
                    )
                }
                com.kael.texinject.TexInjectHookInit.logExternal(
                    "[TexInject/Region] panel=${com.kael.texinject.ui.compose.PanelTouchState.region}"
                )
            }
        } catch (_: Throwable) {
        }
        return if (any) out else null
    }

    fun detach(activity: Activity) {
        mainHandler.post {
            try {
                activePopup?.dismiss()
            } catch (_: Throwable) {
            }
            activePopup = null
            activeOverlay = null
        }
    }

    /** 当前悬浮层 View（供防录屏打图层标记用）。 */
    fun overlayView(): android.view.View? = activeOverlay

    /** 设置里切换防录屏后调用，立即重打标记。 */
    fun refreshAntiRecord() {
        mainHandler.post { AntiRecord.refreshOverlay() }
    }

    /**
     * 强制窗口重算一次可触摸区域。
     *
     * 触摸区靠 ViewTreeObserver.OnComputeInternalInsetsListener 计算，
     * 只有系统走一次 traversal 才会回调。悬浮球被拖动 / 横竖屏切换后坐标变了，
     * 但没人触发 traversal —— region 就停在旧坐标，球看得见却点不到（点击穿透给游戏）。
     */
    fun refreshTouchRegion() {
        mainHandler.post {
            try {
                activePopup?.contentView?.let {
                    it.requestLayout()
                    it.invalidate()
                }
            } catch (_: Throwable) {
            }
        }
    }

    /**
     * 面板打开/关闭时调用。
     *
     * FLAG_NOT_FOCUSABLE 的窗口拿不到窗口级焦点，输入法连不上去（只有光标、没有键盘）。
     * 面板打开期间把窗口切成可聚焦，关掉再还原 —— 平时游戏照常跑。
     */
    fun setPanelFocusable(on: Boolean) {
        mainHandler.post {
            val pw = activePopup ?: return@post
            try {
                if (pw.isFocusable == on) return@post
                pw.isFocusable = on
                pw.inputMethodMode = if (on) {
                    PopupWindow.INPUT_METHOD_NOT_NEEDED
                } else {
                    PopupWindow.INPUT_METHOD_NEEDED
                }
                pw.update()
            } catch (_: Throwable) {
            }
        }
    }

    /**
     * 输入框获得/失去焦点时调用。
     *
     * PopupWindow 平时是 isFocusable=false（不抢游戏焦点），代价是窗口内控件
     * 拿不到输入焦点、打不了字。所以编辑期间临时切成可聚焦，编辑完再切回来。
     */
    fun setImeEditing(editing: Boolean) {
        mainHandler.post {
            val pw = activePopup ?: return@post
            try {
                if (pw.isFocusable == editing) return@post
                pw.isFocusable = editing
                pw.inputMethodMode = if (editing) {
                    PopupWindow.INPUT_METHOD_NOT_NEEDED
                } else {
                    PopupWindow.INPUT_METHOD_NEEDED
                }
                pw.update()
                if (editing) {
                    // 窗口刚变成可聚焦，此刻视图还没拿到窗口焦点，等两帧再把焦点还回去
                    val ov = activeOverlay
                    mainHandler.postDelayed({
                        try {
                            val target = ov?.findFocus() ?: ov
                            target?.requestFocus()
                            val ctx = ov?.context ?: return@postDelayed
                            val imm = ctx.getSystemService(
                                android.view.inputmethod.InputMethodManager::class.java
                            )
                            imm?.showSoftInput(target, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT)
                        } catch (_: Throwable) {
                        }
                    }, 150)
                }
            } catch (_: Throwable) {
            }
        }
    }

    /** PopupWindow 内部标志位操作（无公开 API，走反射）。 */
    internal object PopupFlags {
        /** 加上 FLAG_NOT_TOUCH_MODAL，使窗口外的触摸交给下层。 */
        fun enableTouchPassthrough(pw: PopupWindow) {
            try {
                val f = PopupWindow::class.java.getDeclaredField("mWindowLayoutParams")
                f.isAccessible = true
                val lp = f.get(pw) as? android.view.WindowManager.LayoutParams ?: return
                lp.flags = lp.flags or android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                pw.update()
            } catch (_: Throwable) {
            }
        }
    }
}
