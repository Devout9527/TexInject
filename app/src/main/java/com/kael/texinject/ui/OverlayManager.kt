package com.kael.texinject.ui

import android.app.Activity
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.ViewGroup
import android.widget.PopupWindow

object OverlayManager {
    private const val TAG = "TEXINJECT_OVERLAY"
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var activeOverlay: android.view.View? = null

    @Volatile
    private var activePopup: PopupWindow? = null

    @Volatile
    private var lastRegionLog: String? = null

    fun attach(activity: Activity) {
        if (activity.isFinishing || activity.isDestroyed) return
        if (java.io.File("/data/local/tmp/texinject_no_overlay").exists()) {
            logx("[TexInject] overlay skipped (flag /data/local/tmp/texinject_no_overlay)")
            return
        }
        mainHandler.post {
            try {
                if (activePopup != null) return@post

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

                var created: com.kael.texinject.ui.compose.ComposeHostView? = null
                stage(1, "create ComposeHostView(panel)") {
                    val ov = com.kael.texinject.ui.compose.ComposeHostView(activity) {
                        com.kael.texinject.ui.compose.PanelOverlay(activity, false)
                    }
                    ov.tag = TAG
                    ov.touchRegionProvider = { touchRegion(ov) }
                    created = ov
                }
                val overlay = created
                if (overlay == null) {
                    logx("[TexInject] attach aborted: ComposeHostView not created (stage=$limit)")
                    return@post
                }

                stage(2, "NowPlayingBar") { NowPlayingBar.attach(activity, overlay) }
                stage(3, "NativeBall") {
                    com.kael.texinject.ui.NativeBall.attach(activity, overlay) {
                        com.kael.texinject.ui.PanelBridge.toggle?.invoke()
                    }
                }
                stage(4, "HotkeyLayer(MCP->native python)") { HotkeyLayer.attach(activity, overlay) }
                stage(5, "FeatureHotkeys") { FeatureHotkeys.init(activity) }

                stage(6, "show PopupWindow") {
                    val pw = PopupWindow(
                        overlay,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        false
                    )
                    pw.isTouchable = true
                    pw.isFocusable = false
                    pw.inputMethodMode = PopupWindow.INPUT_METHOD_NEEDED
                    pw.isOutsideTouchable = false
                    pw.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
                    pw.isClippingEnabled = false
                    pw.showAtLocation(
                        activity.window.decorView,
                        Gravity.TOP or Gravity.START, 0, 0
                    )
                    PopupFlags.enableTouchPassthrough(pw)

                    activeOverlay = overlay
                    activePopup = pw
                }

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

    private fun touchRegion(overlay: com.kael.texinject.ui.compose.ComposeHostView): android.graphics.Rect? {
        val out = android.graphics.Rect()
        var any = false
        com.kael.texinject.ui.compose.PanelTouchState.region?.let {
            if (!it.isEmpty) {
                out.set(it)
                any = true
            }
        }
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

    fun overlayView(): android.view.View? = activeOverlay

    fun refreshAntiRecord() {
        mainHandler.post { AntiRecord.refreshOverlay() }
    }

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

    internal object PopupFlags {
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
