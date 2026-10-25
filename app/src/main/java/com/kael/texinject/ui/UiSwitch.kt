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

class UiSwitch @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val thumbPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val shadowPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val trackRect = RectF()
    private var animator: ValueAnimator? = null

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
        trackPaint.color = evaluateColor(progress, offColor, onColor)
        trackRect.set(0f, 0f, w, h)
        canvas.drawRoundRect(trackRect, radius, radius, trackPaint)

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
