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

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import android.view.animation.DecelerateInterpolator

/**
 * MIUI 风格开关。
 * 轨道 ON=#0A84FF / OFF=#E5E7EB，圆形滑块带阴影，带 DecelerateInterpolator 动画。
 */
class UiSwitch @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val thumbPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val shadowPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val trackRect = RectF()
    private var animator: ValueAnimator? = null

    /** 0f = off, 1f = on */
    private var progress = 0f

    var isChecked: Boolean = false
        private set

    private var onColor = UiTheme.switchOn
    private var offColor = UiTheme.switchOff

    var onCheckedChangeListener: ((Boolean) -> Unit)? = null

    init {
        shadowPaint.color = UiTheme.switchThumbShadow
        isClickable = true
        isFocusable = true
    }

    fun setColors(on: Int, off: Int) {
        onColor = on
        offColor = off
        invalidate()
    }

    fun setChecked(checked: Boolean, animate: Boolean = true) {
        if (isChecked == checked) return
        isChecked = checked
        val target = if (checked) 1f else 0f
        if (animate) {
            animator?.cancel()
            animator = ValueAnimator.ofFloat(progress, target).apply {
                duration = 180
                interpolator = DecelerateInterpolator()
                addUpdateListener {
                    progress = it.animatedValue as Float
                    invalidate()
                }
                start()
            }
        } else {
            progress = target
            invalidate()
        }
    }

    fun toggle() {
        setChecked(!isChecked)
        onCheckedChangeListener?.invoke(isChecked)
    }

    private fun evaluateColor(f: Float, a: Int, b: Int): Int {
        val ar = Color.red(a); val ag = Color.green(a); val ab = Color.blue(a); val aa = Color.alpha(a)
        val br = Color.red(b); val bg = Color.green(b); val bb = Color.blue(b); val ba = Color.alpha(b)
        return Color.argb(
            (aa + (ba - aa) * f).toInt(),
            (ar + (br - ar) * f).toInt(),
            (ag + (bg - ag) * f).toInt(),
            (ab + (bb - ab) * f).toInt()
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val h = height.toFloat()
        val w = width.toFloat()
        val radius = h / 2f
        // 轨道
        trackPaint.color = evaluateColor(progress, offColor, onColor)
        trackRect.set(0f, 0f, w, h)
        canvas.drawRoundRect(trackRect, radius, radius, trackPaint)

        // 滑块
        val pad = h * 0.12f
        val thumbR = radius - pad
        val cx = pad + thumbR + (w - 2f * (pad + thumbR)) * progress
        val cy = h / 2f
        canvas.drawCircle(cx, cy, thumbR + pad * 0.4f, shadowPaint)
        thumbPaint.color = Color.WHITE
        canvas.drawCircle(cx, cy, thumbR, thumbPaint)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val h = resolveSize(suggestedMinimumHeight.coerceAtLeast((22 * resources.displayMetrics.density).toInt()), heightMeasureSpec)
        val w = resolveSize((h * 1.9f).toInt(), widthMeasureSpec)
        setMeasuredDimension(w, h)
    }

    override fun performClick(): Boolean {
        toggle()
        return super.performClick()
    }
}
