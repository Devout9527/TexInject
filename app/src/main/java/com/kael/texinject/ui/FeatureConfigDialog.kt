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
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.PopupWindow
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import kotlin.math.roundToInt

/**
 * 功能配置面板（移植 BP_mcp ClickGui 的配置页）。
 * 点模块 -> 读 FEATURE_PARAMS/FEATURE_TOGGLES -> 画 滑块/开关/下拉 -> 改动写回模块属性。
 */
object FeatureConfigDialog {
    private var popup: PopupWindow? = null
    private val main = Handler(Looper.getMainLooper())

    fun show(activity: Activity, module: McpModule) {
        // 先跑 Python 拿配置定义，稍后读回
        McpBridge.refreshConfig(activity, module.name)
        main.postDelayed({ build(activity, module) }, 350)
    }

    fun hide() {
        popup?.let { runCatching { it.dismiss() } }
        popup = null
    }

    private fun build(activity: Activity, module: McpModule) {
        val dm = activity.resources.displayMetrics
        val d: (Float) -> Int = { (it * dm.density + .5f).toInt() }
        val items = McpBridge.loadConfig(activity)

        val card = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#F21A1C1E"))
                cornerRadius = d(16f).toFloat()
                setStroke(d(1f), Color.parseColor("#33FFFFFF"))
            }
            setPadding(d(14f), d(12f), d(14f), d(12f))
        }

        // 标题栏
        val header = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(TextView(activity).apply {
            text = module.display
            setTextColor(Color.WHITE)
            textSize = 16f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        })
        header.addView(TextView(activity).apply {
            text = "×"
            textSize = 18f
            gravity = Gravity.CENTER
            setTextColor(Color.WHITE)
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#33FFFFFF"))
                cornerRadius = d(13f).toFloat()
            }
            isClickable = true
            layoutParams = LinearLayout.LayoutParams(d(26f), d(26f))
            setOnTouchListener { v, ev ->
                if (ev.actionMasked == android.view.MotionEvent.ACTION_UP) { hide(); v.performClick() }
                true
            }
        })
        card.addView(header, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        val scroll = ScrollView(activity).apply {
            isVerticalScrollBarEnabled = true
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        }
        val col = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        scroll.addView(col)

        if (items.isEmpty()) {
            col.addView(TextView(activity).apply {
                text = "该功能没有可配置项（或 MCP 未加载）"
                setTextColor(Color.parseColor("#FF9AA0A8"))
                textSize = 13f
                setPadding(0, d(14f), 0, d(4f))
            })
        } else {
            for (it in items) {
                when (it.kind) {
                    "slider" -> col.addView(buildSlider(activity, module, it, d))
                    "toggle" -> col.addView(buildToggle(activity, module, it, d))
                    "dropdown" -> col.addView(buildDropdown(activity, module, it, d))
                }
            }
        }

        card.addView(scroll)

        val w = (dm.widthPixels * 0.62f).toInt()
        val h = (dm.heightPixels * 0.78f).toInt()
        val pw = PopupWindow(card, w, h, false)
        pw.isTouchable = true
        // 点弹窗外任意处关闭
        pw.isFocusable = true
        pw.isOutsideTouchable = true
        pw.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        pw.setOnDismissListener { popup = null }
        hide()
        runCatching { pw.showAtLocation(activity.window.decorView, Gravity.CENTER, 0, 0) }
        popup = pw
    }

    private fun label(activity: Activity, text: String, d: (Float) -> Int): TextView = TextView(activity).apply {
        this.text = text
        setTextColor(Color.WHITE)
        textSize = 13f
        setPadding(0, d(10f), 0, d(2f))
    }

    private fun buildSlider(activity: Activity, m: McpModule, it: McpBridge.McpConfigItem, d: (Float) -> Int): View {
        val box = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        val step = if (it.step <= 0f) 1f else it.step
        val maxProgress = (((it.max - it.min) / step).roundToInt()).coerceAtLeast(1)
        val cur = it.value.toFloatOrNull() ?: it.min
        fun fmt(v: Float): String = if (step >= 1f) v.roundToInt().toString() else String.format("%.2f", v)

        val lab = label(activity, "${it.label}: ${fmt(cur)}", d)
        val sk = SeekBar(activity).apply {
            max = maxProgress
            progress = (((cur - it.min) / step).roundToInt()).coerceIn(0, maxProgress)
            progressTintList = android.content.res.ColorStateList.valueOf(Color.parseColor("#CC0A84FF"))
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, value: Int, fromUser: Boolean) {
                    if (fromUser) {
                        val v = it.min + value * step
                        lab.text = "${it.label}: ${fmt(v)}"
                        McpBridge.setConfig(activity, m.name, it.attr, v.toString())
                    }
                }
                override fun onStartTrackingTouch(sb: SeekBar) {}
                override fun onStopTrackingTouch(sb: SeekBar) {}
            })
        }
        box.addView(lab)
        box.addView(sk)
        return box
    }

    private fun buildToggle(activity: Activity, m: McpModule, it: McpBridge.McpConfigItem, d: (Float) -> Int): View {
        val row = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, d(10f), 0, d(4f))
        }
        row.addView(TextView(activity).apply {
            text = it.label
            setTextColor(Color.WHITE)
            textSize = 13f
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        })
        val on = it.value == "1" || it.value.equals("true", true)
        row.addView(TextView(activity).apply {
            text = if (on) "开" else "关"
            textSize = 12f
            gravity = Gravity.CENTER
            setTextColor(Color.WHITE)
            background = GradientDrawable().apply {
                setColor(Color.parseColor(if (on) "#CC1FA24A" else "#33FFFFFF"))
                cornerRadius = d(8f).toFloat()
            }
            isClickable = true
            layoutParams = LinearLayout.LayoutParams(d(52f), d(28f))
            var state = on
            setOnTouchListener { v, ev ->
                if (ev.actionMasked == android.view.MotionEvent.ACTION_UP) {
                    state = !state
                    (v as TextView).text = if (state) "开" else "关"
                    v.background = GradientDrawable().apply {
                        setColor(Color.parseColor(if (state) "#CC1FA24A" else "#33FFFFFF"))
                        cornerRadius = d(8f).toFloat()
                    }
                    McpBridge.setConfig(activity, m.name, it.attr, if (state) "true" else "false")
                    v.performClick()
                }
                true
            }
        })
        return row
    }

    private fun buildDropdown(activity: Activity, m: McpModule, it: McpBridge.McpConfigItem, d: (Float) -> Int): View {
        val box = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        box.addView(label(activity, it.label, d))
        val row = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        val chips = mutableListOf<TextView>()
        it.options.forEachIndexed { idx, opt ->
            val optLabel = it.optionLabels.getOrElse(idx) { opt }
            val selected = opt == it.value
            val chip = TextView(activity).apply {
                text = optLabel
                textSize = 12f
                gravity = Gravity.CENTER
                setTextColor(Color.WHITE)
                setPadding(d(10f), d(6f), d(10f), d(6f))
                background = GradientDrawable().apply {
                    setColor(Color.parseColor(if (selected) "#CC0A84FF" else "#33FFFFFF"))
                    cornerRadius = d(8f).toFloat()
                }
                isClickable = true
                layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                    rightMargin = d(6f)
                }
                setOnTouchListener { v, ev ->
                    if (ev.actionMasked == android.view.MotionEvent.ACTION_UP) {
                        McpBridge.setConfig(activity, m.name, it.attr, opt)
                        for (c in chips) c.background = GradientDrawable().apply {
                            setColor(Color.parseColor(if (c == v) "#CC0A84FF" else "#33FFFFFF"))
                            cornerRadius = d(8f).toFloat()
                        }
                        v.performClick()
                    }
                    true
                }
            }
            chips.add(chip)
            row.addView(chip)
        }
        box.addView(row)
        return box
    }
}
