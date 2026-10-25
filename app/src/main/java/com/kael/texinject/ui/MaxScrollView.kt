package com.kael.texinject.ui

import android.content.Context
import android.widget.ScrollView

class MaxScrollView(context: Context, private val maxHeight: Int) : ScrollView(context) {
    init {
        isVerticalScrollBarEnabled = true
        isFillViewport = false
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        if (MeasureSpec.getMode(heightMeasureSpec) == MeasureSpec.EXACTLY) {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec)
            return
        }
        val capped = MeasureSpec.makeMeasureSpec(maxHeight, MeasureSpec.AT_MOST)
        super.onMeasure(widthMeasureSpec, capped)
    }
}
