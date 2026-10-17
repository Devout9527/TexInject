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

    fun attach(activity: Activity) {
        if (activity.isFinishing || activity.isDestroyed) return
        mainHandler.post {
            try {
                if (activePopup != null) return@post

                // Compose 版面板（唯一内容）
                val overlay = com.kael.texinject.ui.compose.ComposeHostView(activity) {
                    com.kael.texinject.ui.compose.PanelOverlay(activity, false)
                }
                overlay.tag = TAG
                // 可触摸区域 = 面板区域 ∪ 所有外部挂进来的 View（播放条/快捷键层）的屏幕矩形。
                // 之前只给了面板区域，导致播放条点不动、拖不动（触摸全落到游戏里）。
                overlay.touchRegionProvider = {
                    val out = android.graphics.Rect()
                    var any = false
                    com.kael.texinject.ui.compose.PanelTouchState.region?.let {
                        if (!it.isEmpty) {
                            out.set(it)
                            any = true
                        }
                    }
                    for (i in 0 until overlay.childCount) {
                        val c = overlay.getChildAt(i)
                        if (c is androidx.compose.ui.platform.ComposeView) continue
                        if (c.visibility != android.view.View.VISIBLE) continue
                        val loc = IntArray(2)
                        c.getLocationOnScreen(loc)
                        val w = c.width
                        val h = c.height
                        if (w <= 0 || h <= 0) continue
                        val r = android.graphics.Rect(loc[0], loc[1], loc[0] + w, loc[1] + h)
                        if (any) out.union(r) else { out.set(r); any = true }
                    }
                    if (any) out else null
                }
                // 以下三行原本都在 OverlayLayout 里，删文件时全漏了
                fun wire(name: String, block: () -> Unit) {
                    try {
                        block()
                    } catch (e: Throwable) {
                        com.kael.texinject.TexInjectHookInit.logExternal(
                            "[TexInject] $name failed: ${e.javaClass.simpleName}: ${e.message}"
                        )
                    }
                }

                wire("NowPlayingBar.attach") { NowPlayingBar.attach(activity, overlay) }
                wire("HotkeyLayer.attach") { HotkeyLayer.attach(activity, overlay) }
                wire("FeatureHotkeys.init") { FeatureHotkeys.init(activity) }

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

                AntiRecord.remember(activity)
                AntiRecord.refreshOverlay()

                com.kael.texinject.TexInjectHookInit.logExternal(
                    "[TexInject] overlay mounted (independent window) to ${activity.javaClass.name}, " +
                        "passthrough=${overlay.passthroughOk}"
                )
            } catch (e: Throwable) {
                com.kael.texinject.TexInjectHookInit.logExternal("[TexInject] attach failed: ${e.message}")
            }
        }
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
