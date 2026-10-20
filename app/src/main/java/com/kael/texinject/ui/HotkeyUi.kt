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
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView

/**
 * 「快捷键」分类：列出 MCP（z1yr 系统）里的所有模块，直接开关它们。
 *
 * - 顶部一排分类 chips（全部/战斗类/移动类/…），点一下只看那一类
 * - 每个模块一行：名称 / [启用开关] / [显示悬浮窗开关]
 * - 启用开关 → 调 MCP 的 module.toggle()（等同聊天敲 .模块名）
 * - 显示悬浮窗 → 该模块的悬浮按钮出现在屏幕上（点击也能 toggle）
 */
object HotkeyUi {

    private val CAT_ORDER = listOf("Combat", "Movement", "World", "Visual", "Misc", "Client")

    fun buildView(activity: Activity, onConfig: ((McpModule) -> Unit)? = null): View {
        val ctx = activity.applicationContext
        val density = activity.resources.displayMetrics.density
        fun dp(v: Float) = (v * density + .5f).toInt()
        val mainHandler = Handler(Looper.getMainLooper())

        var selectedCat = HotkeyStore.selectedCat(ctx)

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT
            )
        }
        fun lp() = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
        )

        // 刷新：做成 chip，稍后放进分类胶囊的最开头
        val refreshBtn = TextView(activity).apply {
            text = "刷新"
            textSize = 12f
            gravity = Gravity.CENTER
            setTextColor(Color.WHITE)
            setPadding(dp(10f), 0, dp(10f), 0)
            isClickable = true
            background = GradientDrawable().apply {
                setColor(UiTheme.glassWhite)
                cornerRadius = dp(7f).toFloat()
                setStroke(dp(1f), UiTheme.glassWhiteBorder)
            }
        }

        val errText = TextView(activity).apply {
            textSize = 11f
            setTextColor(Color.parseColor("#FF8A80"))
            setPadding(0, dp(6f), 0, dp(2f))
            visibility = View.GONE
        }
        root.addView(errText, lp())

        // ---- 顶部分类 chips ----
        val chipRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        // 分类：整格胶囊（仿 py/mcp），选中块撑满高度
        val chipBox = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(3f), dp(3f), dp(3f), dp(3f))
            background = GradientDrawable().apply {
                setColor(UiTheme.glassWhite)
                cornerRadius = dp(10f).toFloat()
                setStroke(dp(1f), UiTheme.glassWhiteBorder)
            }
        }
        val chipScroll = HorizontalScrollView(activity).apply {
            isHorizontalScrollBarEnabled = false
            addView(chipBox)
        }
        root.addView(chipScroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(36f)
        ).apply { topMargin = dp(6f) })

        val headRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(6f), 0, dp(2f))
        }
        headRow.addView(TextView(activity).apply {
            text = "模块"
            textSize = 11f
            setTextColor(Color.parseColor("#9AA7B2"))
        }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        headRow.addView(TextView(activity).apply {
            text = "启用  悬浮窗"
            textSize = 11f
            setTextColor(Color.parseColor("#9AA7B2"))
        })

        // 列表放进 ScrollView 并吃掉剩余高度，否则条目多了滑不动
        val listHolder = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        val listScroll = android.widget.ScrollView(activity).apply {
            isFillViewport = true
            isVerticalScrollBarEnabled = true
            addView(listHolder, android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT
            ))
        }
        root.addView(listScroll, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))

        var reloadFn: (() -> Unit)? = null

        fun chip(label: String, value: String) {
            val sel = selectedCat == value
            chipBox.addView(TextView(activity).apply {
                text = label
                textSize = 12f
                gravity = Gravity.CENTER
                setTextColor(if (sel) Color.WHITE else UiTheme.glassWhiteText)
                setPadding(dp(12f), dp(6f), dp(12f), dp(6f))
                background = GradientDrawable().apply {
                    setColor(if (sel) Color.parseColor("#CC0A84FF") else UiTheme.glassWhite)
                    cornerRadius = dp(7f).toFloat()
                    setStroke(dp(1f), if (sel) Color.parseColor("#660A84FF") else UiTheme.glassWhiteBorder)
                }
                isClickable = true
                setOnClickListener {
                    selectedCat = value
                    HotkeyStore.setSelectedCat(ctx, value)
                    reloadFn?.invoke()
                }
            }, LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT
            ).apply { marginEnd = dp(3f) })
        }

        fun addRow(m: McpModule) {
            val row = LinearLayout(activity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(0, dp(5f), 0, dp(5f))
            }
            val label = TextView(activity).apply {
                text = m.display
                textSize = 13f
                setTextColor(if (m.enabled) Color.parseColor("#7CFC9B") else Color.WHITE)
                isClickable = true
                setOnClickListener { if (onConfig != null) onConfig.invoke(m) else FeatureConfigDialog.show(activity, m) }
            }
            row.addView(label, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))

            // 齿轮：点开该功能的配置面板
            row.addView(TextView(activity).apply {
                text = "⚙"
                textSize = 15f
                gravity = Gravity.CENTER
                setTextColor(Color.parseColor("#9AA0A8"))
                isClickable = true
                layoutParams = LinearLayout.LayoutParams(dp(26f), dp(26f)).apply { marginEnd = dp(6f) }
                setOnClickListener { if (onConfig != null) onConfig.invoke(m) else FeatureConfigDialog.show(activity, m) }
            })

            val enSw = UiSwitch(activity).apply {
                setChecked(m.enabled, false)
                onCheckedChangeListener = { on ->
                    label.setTextColor(if (on) Color.parseColor("#7CFC9B") else Color.WHITE)
                    McpBridge.toggle(ctx, m.name)
                    mainHandler.postDelayed({
                        McpBridge.refresh(ctx)
                        mainHandler.postDelayed({ reloadFn?.invoke() }, 250)
                    }, 150)
                }
            }
            row.addView(enSw, LinearLayout.LayoutParams(dp(52f), dp(30f)).apply { marginEnd = dp(8f) })

            val shSw = UiSwitch(activity).apply {
                setChecked(HotkeyStore.isShown(ctx, m.name), false)
                onCheckedChangeListener = { on ->
                    HotkeyStore.setShown(ctx, m.name, on)
                    HotkeyLayer.rebuild(ctx)
                }
            }
            row.addView(shSw, LinearLayout.LayoutParams(dp(52f), dp(30f)))

            listHolder.addView(row, lp())
        }

        fun reload() {
            listHolder.removeAllViews()
            listHolder.addView(headRow, lp())

            val err = McpBridge.lastError(ctx)
            if (err != null) {
                errText.text = "⚠ " + err
                errText.visibility = View.VISIBLE
            } else {
                errText.visibility = View.GONE
            }

            val mods = McpBridge.load(ctx)

            // 分类 chips：先清空（原来清的是废弃的 chipRow，导致越刷越多、选中态不跟随）
            chipBox.removeAllViews()
            chipBox.addView(refreshBtn, LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.MATCH_PARENT
            ).apply { marginEnd = dp(3f) })
            chip("全部", "")
            val cats = mods.map { it.category }.distinct().sortedBy {
                val i = CAT_ORDER.indexOf(it)
                if (i >= 0) i else 99
            }
            for (c in cats) {
                chip(mods.firstOrNull { it.category == c }?.categoryDisplay ?: c, c)
            }
            if (selectedCat.isNotEmpty() && selectedCat !in cats) selectedCat = ""

            if (mods.isEmpty()) {
                listHolder.addView(TextView(activity).apply {
                    text = "没有读到模块。先确认 MCP 已加载，再点「刷新模块」。"
                    textSize = 12f
                    setTextColor(Color.parseColor("#8899A6"))
                    setPadding(0, dp(8f), 0, 0)
                }, lp())
                return
            }

            val shown = if (selectedCat.isEmpty()) mods else mods.filter { it.category == selectedCat }

            val groups = LinkedHashMap<String, MutableList<McpModule>>()
            for (m in shown) groups.getOrPut(m.category) { mutableListOf() }.add(m)
            val sortedKeys = groups.keys.sortedBy {
                val i = CAT_ORDER.indexOf(it)
                if (i >= 0) i else 99
            }
            for (key in sortedKeys) {
                val list = groups[key] ?: continue
                // 选了具体分类就不再重复显示小标题
                if (selectedCat.isEmpty()) {
                    listHolder.addView(TextView(activity).apply {
                        text = list.firstOrNull()?.categoryDisplay ?: key
                        textSize = 12f
                        typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
                        setTextColor(Color.parseColor("#7FB2FF"))
                        setPadding(0, dp(10f), 0, dp(2f))
                    }, lp())
                }
                for (m in list) addRow(m)
            }
            HotkeyLayer.refreshStates(ctx)
        }
        reloadFn = ::reload

        refreshBtn.setOnClickListener {
            refreshBtn.text = "…"
            McpBridge.refresh(ctx)
            mainHandler.postDelayed({
                reload()
                refreshBtn.text = "刷新"
            }, 500)
        }

        reload()
        return root
    }
}
