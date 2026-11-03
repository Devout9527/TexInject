/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 扩展功能的独立悬浮窗。
 *
 * 为什么要单独一层：面板本身在 PopupWindow 里，切到别的分类就把内容盖掉了；
 * NBT 编辑器、Cookie 管理这类要一边看游戏一边改，得能浮在外面。
 *
 * 用 Dialog 而不是 WindowManager：不需要悬浮窗权限，也不用碰 Type 限制，
 * 在游戏进程内直接挂在 Activity 上就行。代价是它会吃掉背后触摸，
 * 所以给了标题栏拖动 + 右上角关闭。
 */
package com.kael.texinject.ui

import android.app.Activity
import android.app.Dialog
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.compose.runtime.Composable
import com.kael.texinject.ui.compose.ComposeHost

object FloatingHost {

    private var current: Dialog? = null

    /** 当前是否已经有悬浮窗开着。 */
    fun isShowing(): Boolean = current?.isShowing == true

    fun close() {
        runCatching { current?.dismiss() }
        current = null
    }

    /**
     * 弹一个可拖动的悬浮窗。
     *
     * @param widthDp  窗宽
     * @param heightDp 窗高
     */
    fun show(
        activity: Activity,
        title: String,
        widthDp: Int = 330,
        heightDp: Int = 430,
        content: @Composable () -> Unit,
    ): Dialog? {
        if (activity.isFinishing || activity.isDestroyed) return null
        close()

        val density = activity.resources.displayMetrics.density
        fun dp(v: Int) = (v * density + 0.5f).toInt()

        val dialog = Dialog(activity, android.R.style.Theme_Translucent_NoTitleBar)
        dialog.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)
        dialog.setCancelable(false)

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#F01C1C1E"))
                cornerRadius = dp(14).toFloat()
            }
        }

        // 标题栏：拖动 + 关闭
        val head = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(12), dp(8), dp(6), dp(8))
        }
        head.addView(TextView(activity).apply {
            text = title
            setTextColor(Color.WHITE)
            textSize = 13f
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))

        head.addView(TextView(activity).apply {
            text = "×"
            setTextColor(Color.parseColor("#8E8E93"))
            textSize = 18f
            gravity = Gravity.CENTER
            setPadding(dp(10), 0, dp(10), 0)
            setOnClickListener { close() }
        })
        root.addView(head, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        val body = FrameLayout(activity)
        root.addView(body, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))

        // 必须走 ComposeHost：游戏进程的视图树上没有 ViewTreeLifecycleOwner，
        // 直接 ComposeView(activity).setContent{} 会在 onAttachedToWindow 抛
        // IllegalStateException: ViewTreeLifecycleOwner not found。
        val compose = ComposeHost.create(activity, root, content)
        body.addView(compose, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        dialog.setContentView(root)

        val win = dialog.window
        if (win != null) {
            win.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            val lp = win.attributes
            lp.width = dp(widthDp)
            lp.height = dp(heightDp)
            lp.gravity = Gravity.TOP or Gravity.START
            lp.x = dp(24)
            lp.y = dp(120)
            win.attributes = lp
        }

        // 拖标题栏移动整个窗
        head.setOnTouchListener(object : View.OnTouchListener {
            private var downX = 0f
            private var downY = 0f
            private var startX = 0
            private var startY = 0

            override fun onTouch(v: View, e: MotionEvent): Boolean {
                val w = dialog.window ?: return false
                when (e.getActionMasked()) {
                    MotionEvent.ACTION_DOWN -> {
                        downX = e.getRawX()
                        downY = e.getRawY()
                        startX = w.attributes.x
                        startY = w.attributes.y
                        return true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val lp = w.attributes
                        lp.x = startX + (e.getRawX() - downX).toInt()
                        lp.y = startY + (e.getRawY() - downY).toInt()
                        w.attributes = lp
                        return true
                    }
                }
                return false
            }
        })

        runCatching { dialog.show() }
        current = dialog
        return dialog
    }
}
