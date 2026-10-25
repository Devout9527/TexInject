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
            setLayout(
                minOf(d(540f), (dm.widthPixels * 0.94f).toInt()),
                WRAP_CONTENT
            )
            val lp = attributes
            lp.gravity = Gravity.CENTER
            attributes = lp
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

    fun showPair(
        activity: Activity,
        title: String,
        nameHint: String,
        nameInit: String,
        dataHint: String,
        dataInit: String,
        onResult: (String, String) -> Unit
    ) {
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

        fun label(t: String) = TextView(activity).apply {
            text = t
            textSize = 12f
            setTextColor(sub)
        }

        fun field(initial: String, hint: String, single: Boolean, lines: Int) = EditText(activity).apply {
            setText(initial)
            setHint(hint)
            setSelection(initial.length)
            textSize = 14f
            setTextColor(Color.WHITE)
            setHintTextColor(Color.parseColor("#66707A"))
            isSingleLine = single
            if (!single) {
                inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
                setLines(lines)
                gravity = Gravity.TOP or Gravity.START
            }
            setPadding(d(12f), d(10f), d(12f), d(10f))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#22FFFFFF"))
                cornerRadius = d(10f).toFloat()
            }
        }

        box.addView(label(nameHint), LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = d(12f)
        })
        val etName = field(nameInit, "留空则用内容里的账号 ID", true, 1)
        box.addView(etName, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = d(6f)
        })

        box.addView(label(dataHint), LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = d(12f)
        })
        val etData = field(dataInit, "粘贴 cookie 内容", false, 6)
        box.addView(etData, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = d(6f)
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
            onResult(etName.text?.toString().orEmpty(), etData.text?.toString().orEmpty())
            dlg.dismiss()
        })
        box.addView(bar, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = d(6f)
        })

        dlg.setContentView(box)
        dlg.window?.apply {
            setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            setLayout(
                minOf(d(540f), (dm.widthPixels * 0.94f).toInt()),
                WRAP_CONTENT
            )
            val lp = attributes
            lp.gravity = Gravity.CENTER
            attributes = lp
        }
        dlg.show()

        etName.requestFocus()
        etName.postDelayed({
            etName.requestFocus()
            val imm = activity.getSystemService(Activity.INPUT_METHOD_SERVICE) as InputMethodManager
            imm.showSoftInput(etName, InputMethodManager.SHOW_IMPLICIT)
        }, 120)
    }
}
