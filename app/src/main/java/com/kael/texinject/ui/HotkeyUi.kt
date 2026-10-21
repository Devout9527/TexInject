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

        val chipRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
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
