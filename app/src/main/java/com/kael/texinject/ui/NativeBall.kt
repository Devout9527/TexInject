/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
package com.kael.texinject.ui

import android.app.Activity
import android.graphics.Color
import android.graphics.Outline
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.ViewOutlineProvider
import android.widget.FrameLayout
import android.widget.ImageView
import com.kael.texinject.GamePaths

/**
 * 原生悬浮球（移植自 Compose 化之前的 OverlayLayout.buildFloatingBall）。
 *
 * 为什么不用 Compose 画：
 * Compose 的球不是 View，窗口计算可触摸区域时看不见它，只能手动上报坐标，
 * 一旦拖动就和实际位置对不上（点不动）。原生 View 是 overlay 的真子 View，
 * touchRegionProvider 直接取它的 getGlobalVisibleRect；移动时改 layoutParams
 * 必然触发 requestLayout → insets 重算，天然同步。
 */
object NativeBall {

    private var ball: FrameLayout? = null
    private var posX = 0
    private var posY = 0
    private var dragging = false

    fun attach(activity: Activity, parent: ViewGroup, onClick: () -> Unit) {
        if (ball != null) return
        val dp = activity.resources.displayMetrics.density
        fun d(v: Float) = (v * dp + .5f).toInt()
        val size = d(40f)
        val prefs = activity.getSharedPreferences("texinject_overlay", Activity.MODE_PRIVATE)
        posX = prefs.getInt("x", d(16f))
        posY = prefs.getInt("y", d(64f))

        val b = FrameLayout(activity).apply {
            layoutParams = FrameLayout.LayoutParams(size, size, Gravity.TOP or Gravity.START).apply {
                leftMargin = posX
                topMargin = posY
            }
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#CC1E1E22"))
                cornerRadius = d(12f).toFloat()
            }
            elevation = d(4f).toFloat()
            clipToOutline = true
            outlineProvider = object : ViewOutlineProvider() {
                override fun getOutline(v: View, outline: Outline) {
                    outline.setRoundRect(0, 0, v.width, v.height, d(12f).toFloat())
                }
            }
        }

        val video = IconLoader.findBallVideo(activity, GamePaths.sourceDir)
        if (video != null) {
            val cv = CenterCropVideo(activity)
            cv.attach(b)
            cv.play(video.absolutePath)
        } else {
            b.addView(ImageView(activity).apply {
                layoutParams = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )
                setImageDrawable(IconLoader.getLogoDrawable(activity, GamePaths.sourceDir))
                scaleType = ImageView.ScaleType.FIT_CENTER
            })
        }

        setupDrag(activity, b, prefs)
        b.setOnClickListener { onClick() }
        parent.addView(b)
        ball = b
    }

    private fun applyPos(v: View) {
        (v.layoutParams as? FrameLayout.LayoutParams)?.let {
            it.gravity = Gravity.TOP or Gravity.START
            it.leftMargin = posX
            it.topMargin = posY
            v.layoutParams = it
        }
    }

    /** 长按 1 秒进入拖动；松手前只算点击。moved 只在单次触摸内有效，不跨手势保留。 */
    private fun setupDrag(activity: Activity, b: View, prefs: android.content.SharedPreferences) {
        val enterDrag = Runnable {
            dragging = true
            b.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        }
        val tol = activity.resources.displayMetrics.density * 26f
        b.setOnTouchListener(object : View.OnTouchListener {
            private var downRawX = 0f
            private var downRawY = 0f
            private var startX = 0
            private var startY = 0
            private var moved = false

            override fun onTouch(v: View, e: MotionEvent): Boolean {
                when (e.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        downRawX = e.rawX; downRawY = e.rawY
                        startX = posX; startY = posY
                        moved = false
                        dragging = false
                        v.isPressed = true
                        v.parent?.requestDisallowInterceptTouchEvent(true)
                        v.removeCallbacks(enterDrag)
                        v.postDelayed(enterDrag, 1000L)
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val dx = e.rawX - downRawX
                        val dy = e.rawY - downRawY
                        if (!dragging && dx * dx + dy * dy > tol * tol) {
                            moved = true
                            v.removeCallbacks(enterDrag)
                        }
                        if (dragging) {
                            posX = (startX + dx).toInt().coerceAtLeast(0)
                            posY = (startY + dy).toInt().coerceAtLeast(0)
                            applyPos(v)
                        }
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        v.removeCallbacks(enterDrag)
                        v.isPressed = false
                        if (dragging) {
                            prefs.edit().putInt("x", posX).putInt("y", posY).apply()
                            dragging = false
                        } else if (e.actionMasked == MotionEvent.ACTION_UP && !moved) {
                            v.performClick()
                        }
                    }
                }
                return true
            }
        })
    }

    fun detach() {
        ball?.let { (it.parent as? ViewGroup)?.removeView(it) }
        ball = null
    }
}

/** 原生球与 Compose 面板之间的开合桥。 */
object PanelBridge {
    @Volatile
    var toggle: (() -> Unit)? = null
}
