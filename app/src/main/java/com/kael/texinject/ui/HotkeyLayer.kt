package com.kael.texinject.ui

import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.widget.PopupWindow
import android.widget.TextView

object HotkeyLayer {

    @Volatile private var activityRef: Activity? = null
    private val entries = mutableListOf<Triple<String, PopupWindow, TextView>>()
    @Volatile private var started = false
    private val mainHandler = Handler(Looper.getMainLooper())

    private const val ON_BG = "#CC0A84FF"
    private const val OFF_BG = "#CC1A1C1E"
    private const val BORDER = "#33FFFFFF"

    fun attach(activity: Activity, parent: ViewGroup) {
        if (started) return
        started = true
        activityRef = activity
        rebuild(activity)
        startPolling(activity)
    }

    fun rebuild(context: Context) {
        val act = activityRef ?: return
        mainHandler.post {
            dismissAll()
            if (!HotkeyStore.hasActive()) return@post
            val shown = HotkeyStore.load(act).filter { it.shown }.map { it.name }
            if (shown.isEmpty()) return@post
            val mods = try {
                McpBridge.load(act).associateBy { it.name }
            } catch (e: Throwable) {
                emptyMap()
            }
            shown.forEachIndexed { index, name -> createPopup(act, name, index, mods[name]) }
            McpBridge.refresh(act)
        }
    }

    private fun dismissAll() {
        for ((_, pw, _) in entries) {
            try {
                pw.dismiss()
            } catch (e: Throwable) {
            }
        }
        entries.clear()
    }

    private fun createPopup(act: Activity, name: String, index: Int, mod: McpModule?) {
        val density = act.resources.displayMetrics.density
        fun dp(v: Float) = (v * density + .5f).toInt()
        val screenH = act.resources.displayMetrics.heightPixels

        val saved = HotkeyStore.pos(act, name)
        var posX = saved?.first?.toInt() ?: dp(8f)
        var posY = saved?.second?.toInt() ?: (screenH - dp(90f) - (index + 1) * dp(46f))

        val pw = PopupWindow(
            null,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        )
        pw.isTouchable = true
        pw.isFocusable = false
        pw.isOutsideTouchable = false
        pw.setBackgroundDrawable(ColorDrawable(0))
        pw.isClippingEnabled = false

        val tv = TextView(act).apply {
            text = mod?.display ?: name
            tag = name
            textSize = 12f
            setTextColor(Color.WHITE)
            setPadding(dp(12f), dp(8f), dp(12f), dp(8f))
            background = pill(::dp, mod?.enabled == true)
            isClickable = true
            isFocusable = false
        }

        val handler = Handler(Looper.getMainLooper())
        var activePid = -1
        var dragging = false
        var downX = 0f
        var downY = 0f
        var startX = 0
        var startY = 0
        val slop = ViewConfiguration.get(act).scaledTouchSlop
        val pressTask = Runnable {
            dragging = true
            tv.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        }

        tv.setOnTouchListener { v, ev ->
            val idx = ev.actionIndex
            val pid = if (idx in 0 until ev.pointerCount) ev.getPointerId(idx) else -1
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (activePid == -1 && pid >= 0) {
                        activePid = pid
                        dragging = false
                        downX = ev.getRawX()
                        downY = ev.getRawY()
                        startX = posX
                        startY = posY
                        handler.postDelayed(pressTask, 500)
                    }
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (pid == activePid && pid >= 0) {
                        if (dragging) {
                            posX = startX + (ev.getRawX() - downX).toInt()
                            posY = startY + (ev.getRawY() - downY).toInt()
                            try {
                                pw.update(posX, posY, -1, -1)
                            } catch (e: Throwable) {
                            }
                        } else if (Math.hypot(
                                (ev.getRawX() - downX).toDouble(),
                                (ev.getRawY() - downY).toDouble()
                            ) > slop
                        ) {
                            handler.removeCallbacks(pressTask)
                        }
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    if (ev.actionMasked == MotionEvent.ACTION_CANCEL || pid == activePid) {
                        handler.removeCallbacks(pressTask)
                        val wasDrag = dragging
                        dragging = false
                        activePid = -1
                        if (wasDrag) {
                            HotkeyStore.setPos(act, name, posX.toFloat(), posY.toFloat())
                        } else if (ev.actionMasked != MotionEvent.ACTION_CANCEL) {
                            McpBridge.toggle(act, name)
                            mainHandler.postDelayed({
                                McpBridge.refresh(act)
                                mainHandler.postDelayed({ refreshStates(act) }, 200)
                            }, 150)
                        }
                    }
                    true
                }
                else -> true
            }
        }

        pw.contentView = tv
        try {
            pw.showAtLocation(act.window.decorView, Gravity.TOP or Gravity.START, posX, posY)
            AntiRecord.securePopup(pw, act)
        } catch (e: Throwable) {
            return
        }
        entries.add(Triple(name, pw, tv))
    }

    private fun pill(dp: (Float) -> Int, on: Boolean) = GradientDrawable().apply {
        setColor(Color.parseColor(if (on) ON_BG else OFF_BG))
        cornerRadius = dp(10f).toFloat()
        setStroke(dp(1f), Color.parseColor(BORDER))
    }

    fun refreshStates(context: Context) {
        val mods = try {
            McpBridge.load(context).associateBy { it.name }
        } catch (e: Throwable) {
            return
        }
        val density = context.resources.displayMetrics.density
        fun dp(v: Float) = (v * density + .5f).toInt()
        mainHandler.post {
            for ((name, _, tv) in entries) {
                val m = mods[name]
                if (m != null) tv.text = m.display
                tv.background = pill(::dp, m?.enabled == true)
            }
        }
    }

    private fun startPolling(activity: Activity) {
        val t = Thread({
            while (true) {
                try {
                    McpBridge.refresh(activity)
                    val mods = McpBridge.load(activity)
                    HotkeyStore.prune(activity, mods.map { it.name }.toSet())
                    refreshStates(activity)
                } catch (e: Throwable) {
                }
                try {
                    Thread.sleep(1500)
                } catch (e: InterruptedException) {
                    break
                }
            }
        }, "hotkey-poll")
        t.isDaemon = true
        t.start()
    }
}
