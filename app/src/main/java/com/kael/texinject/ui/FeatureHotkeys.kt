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
import java.util.concurrent.ConcurrentHashMap

/**
 * 功能悬浮快捷键：给「箱子光环」「箱子小偷」这种我们自己的功能一个独立悬浮按钮
 * （PopupWindow 小窗口，独立输入目标 → 多指可用，免悬浮窗权限）。
 *
 * - show(key, label, isOn, onToggle)：显示一个按钮（已存在则只刷新状态）
 * - setOn(key, on)：刷新按钮颜色（绿=开 / 灰=关）
 * - hide(key)：隐藏
 * 点击 = onToggle()；长按 0.5 秒 = 拖动（位置按 key 记忆）
 */
object FeatureHotkeys {
    private const val PREFS = "texinject_feature_hotkeys"

    @Volatile private var activityRef: Activity? = null
    private val buttons = ConcurrentHashMap<String, PopupWindow>()
    private val labels = ConcurrentHashMap<String, String>()
    private val states = ConcurrentHashMap<String, Boolean>()
    private val toggles = ConcurrentHashMap<String, () -> Unit>()
    private val mainHandler = Handler(Looper.getMainLooper())

    fun init(activity: Activity) {
        activityRef = activity
    }

    fun show(key: String, label: String, isOn: Boolean, onToggle: () -> Unit) {
        val act = activityRef ?: return
        labels[key] = label
        toggles[key] = onToggle
        states[key] = isOn
        if (buttons.containsKey(key)) {
            refresh(key)
            return
        }
        mainHandler.post { createButton(act, key) }
    }

    fun hide(key: String) {
        mainHandler.post {
            buttons.remove(key)?.let { runCatching { it.dismiss() } }
            toggles.remove(key)
            states.remove(key)
            labels.remove(key)
        }
    }

    fun setOn(key: String, on: Boolean) {
        states[key] = on
        refresh(key)
    }

    private fun refresh(key: String) {
        val pw = buttons[key] ?: return
        val on = states[key] ?: false
        val label = labels[key] ?: key
        mainHandler.post {
            (pw.contentView as? TextView)?.apply {
                text = label
                background = pill(on)
            }
        }
    }

    private fun prefs(c: Context) =
        c.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    private fun pos(c: Context, key: String): Pair<Int, Int> {
        val p = prefs(c)
        return p.getInt("${key}_x", -1) to p.getInt("${key}_y", -1)
    }

    private fun savePos(c: Context, key: String, x: Int, y: Int) {
        prefs(c).edit().putInt("${key}_x", x).putInt("${key}_y", y).apply()
    }

    private fun createButton(act: Activity, key: String) {
        val density = act.resources.displayMetrics.density
        fun dp(v: Float) = (v * density + .5f).toInt()
        val screenH = act.resources.displayMetrics.heightPixels
        val (sx, sy) = pos(act, key)
        var posX = if (sx >= 0) sx else dp(8f)
        var posY = if (sy >= 0) sy else (screenH - dp(150f))

        val pw = PopupWindow(null, ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT)
        pw.isTouchable = true
        pw.isFocusable = false
        pw.isOutsideTouchable = false
        pw.setBackgroundDrawable(ColorDrawable(0))
        pw.isClippingEnabled = false

        val on = states[key] ?: false
        val tv = TextView(act).apply {
            text = labels[key] ?: key
            textSize = 12f
            setTextColor(Color.WHITE)
            setPadding(dp(12f), dp(8f), dp(12f), dp(8f))
            background = pill(on)
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

        tv.setOnTouchListener { _, ev ->
            val idx = ev.actionIndex
            val pid = if (idx in 0 until ev.pointerCount) ev.getPointerId(idx) else -1
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (activePid == -1 && pid >= 0) {
                        activePid = pid
                        dragging = false
                        downX = ev.rawX; downY = ev.rawY
                        startX = posX; startY = posY
                        handler.postDelayed(pressTask, 500)
                    }
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (pid == activePid) {
                        if (dragging) {
                            posX = startX + (ev.rawX - downX).toInt()
                            posY = startY + (ev.rawY - downY).toInt()
                            runCatching { pw.update(posX, posY, -1, -1) }
                        } else if (Math.hypot(
                                (ev.rawX - downX).toDouble(),
                                (ev.rawY - downY).toDouble()
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
                            savePos(act, key, posX, posY)
                        } else if (ev.actionMasked != MotionEvent.ACTION_CANCEL) {
                            toggles[key]?.invoke()
                        }
                    }
                    true
                }
                else -> true
            }
        }

        pw.contentView = tv
        runCatching {
            pw.showAtLocation(act.window.decorView, Gravity.TOP or Gravity.START, posX, posY)
            buttons[key] = pw
        }
    }

    private fun pill(on: Boolean) = GradientDrawable().apply {
        setColor(Color.parseColor(if (on) "#CC1E7B3A" else "#CC1A1C1E"))
        cornerRadius = 28f
        setStroke(1, Color.parseColor("#33FFFFFF"))
    }
}
