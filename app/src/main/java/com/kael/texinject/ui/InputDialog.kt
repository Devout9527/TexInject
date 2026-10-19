/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
package com.kael.texinject.ui

import android.app.Activity
import android.app.Dialog
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.view.Gravity
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView

/**
 * 文本输入弹窗。
 *
 * 为什么不用面板内的 BasicTextField：
 * 面板挂在 FLAG_NOT_FOCUSABLE 的 PopupWindow 里，游戏进程里拿不到窗口级焦点，
 * 输入法连不上去（只出光标、不出键盘）。Dialog 是独立可聚焦窗口，IME 正常。
 *
 * 样式对齐面板：深色 #1C1C1E 圆角卡、白字、蓝色确定。
 */
object InputDialog {

    fun show(activity: Activity, title: String, initial: String, onResult: (String) -> Unit) {
        val dm = activity.resources.displayMetrics
        val d: (Float) -> Int = { (it * dm.density + .5f).toInt() }
        val accent = Color.parseColor("#0A84FF")
        val sub = Color.parseColor("#8E8E93")

        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(d(18f), d(16f), d(18f), d(8f))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#1C1C1E"))
                cornerRadius = d(18f).toFloat()
            }
        }

        box.addView(TextView(activity).apply {
            text = title
            textSize = 13f
            setTextColor(sub)
        })

        val et = EditText(activity).apply {
            setText(initial)
            setSelection(initial.length)
            textSize = 15f
            setTextColor(Color.WHITE)
            setHintTextColor(Color.parseColor("#66707A"))
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT
            setPadding(d(12f), d(10f), d(12f), d(10f))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#22FFFFFF"))
                cornerRadius = d(10f).toFloat()
            }
        }
        box.addView(et, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = d(10f)
        })

        val dlg = Dialog(activity)
        dlg.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)

        val bar = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
        }
        fun btn(text: String, color: Int, click: () -> Unit): TextView =
            TextView(activity).apply {
                this.text = text
                textSize = 14f
                setTextColor(color)
                gravity = Gravity.CENTER
                setPadding(d(16f), d(12f), d(16f), d(12f))
                isClickable = true
                setOnClickListener { click() }
            }

        bar.addView(btn("取消", sub) { dlg.dismiss() })
        bar.addView(btn("确定", accent) {
            onResult(et.text?.toString().orEmpty())
            dlg.dismiss()
        })
        box.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = d(6f)
        })

        dlg.setContentView(box)
        dlg.window?.apply {
            setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            // 与主面板同宽：min(540dp, 94%屏宽)
            setLayout(
                minOf(d(540f), (dm.widthPixels * 0.94f).toInt()),
                WRAP_CONTENT
            )
            val lp = attributes
            lp.gravity = Gravity.CENTER
            attributes = lp
            // 弹窗自带焦点，键盘直接弹
            setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE or
                    WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE
            )
        }
        dlg.show()

        et.requestFocus()
        et.postDelayed({
            et.requestFocus()
            et.setSelection(et.text?.length ?: 0)
            val imm = activity.getSystemService(Activity.INPUT_METHOD_SERVICE) as InputMethodManager
            imm.showSoftInput(et, InputMethodManager.SHOW_IMPLICIT)
        }, 120)
    }
}
