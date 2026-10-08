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
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import com.kael.texinject.MusicPlayer

/**
 * 「设置」分类：音乐播放模式 + 悬浮播放条尺寸。
 */
object SettingsUi {

    fun buildView(activity: Activity): View {
        val density = activity.resources.displayMetrics.density
        fun d(v: Float) = (v * density + .5f).toInt()

        val root = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }

        fun sectionTitle(text: String): TextView = TextView(activity).apply {
            this.text = text
            textSize = 13f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setTextColor(Color.parseColor("#A9A9A9"))
            setPadding(0, d(4f), 0, d(6f))
        }

        // ---------------- 播放模式 ----------------
        root.addView(sectionTitle("音乐播放模式"))
        val modeLabels = listOf("顺序", "随机", "单曲循环")
        val modeChips = ArrayList<TextView>()
        val modeRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        fun refreshModes() {
            val current = MusicPlayer.getPlayMode(activity)
            modeChips.forEachIndexed { i, chip ->
                val selected = i == current
                chip.setTextColor(if (selected) Color.WHITE else Color.parseColor("#141414"))
                chip.background = GradientDrawable().apply {
                    setColor(if (selected) Color.parseColor("#FF0062FF") else Color.parseColor("#59FFFFFF"))
                    cornerRadius = 9f * density
                    setStroke((1f * density).toInt(), Color.parseColor("#66FFFFFF"))
                }
            }
        }
        for ((i, label) in modeLabels.withIndex()) {
            val chip = TextView(activity).apply {
                text = label
                textSize = 13f
                gravity = Gravity.CENTER
                isClickable = true
                setPadding(0, d(8f), 0, d(8f))
                setOnClickListener {
                    MusicPlayer.setPlayMode(activity, i)
                    refreshModes()
                }
            }
            modeChips.add(chip)
            modeRow.addView(
                chip,
                LinearLayout.LayoutParams(0, d(40f), 1f).apply {
                    leftMargin = d(3f); rightMargin = d(3f)
                }
            )
        }
        root.addView(modeRow, lp())
        refreshModes()

        // ---------------- 播放条尺寸 ----------------
        root.addView(sectionTitle("悬浮播放条尺寸"))
        val widthLabel = TextView(activity).apply {
            textSize = 12f
            setTextColor(Color.WHITE)
            setPadding(0, d(4f), 0, 0)
        }
        val heightLabel = TextView(activity).apply {
            textSize = 12f
            setTextColor(Color.WHITE)
            setPadding(0, d(8f), 0, 0)
        }

        var currentW = NowPlayingBar.getWidthDp(activity)
        var currentH = NowPlayingBar.getHeightDp(activity)
        widthLabel.text = "宽度：${currentW}dp"
        heightLabel.text = "高度：${currentH}dp"

        val widthSeek = SeekBar(activity).apply {
            max = NowPlayingBar.MAX_W - NowPlayingBar.MIN_W
            progress = currentW - NowPlayingBar.MIN_W
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, value: Int, fromUser: Boolean) {
                    currentW = NowPlayingBar.MIN_W + value
                    widthLabel.text = "宽度：${currentW}dp"
                    NowPlayingBar.setSize(activity, currentW, currentH)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }
        val heightSeek = SeekBar(activity).apply {
            max = NowPlayingBar.MAX_H - NowPlayingBar.MIN_H
            progress = currentH - NowPlayingBar.MIN_H
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: SeekBar?, value: Int, fromUser: Boolean) {
                    currentH = NowPlayingBar.MIN_H + value
                    heightLabel.text = "高度：${currentH}dp"
                    NowPlayingBar.setSize(activity, currentW, currentH)
                }
                override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: SeekBar?) {}
            })
        }

        root.addView(widthLabel, lp())
        root.addView(widthSeek, lp())
        root.addView(heightLabel, lp())
        root.addView(heightSeek, lp())

        root.addView(TextView(activity).apply {
            text = "拖动滑块即时生效并记住。文字会随尺寸自动跑马灯滚动。"
            textSize = 11f
            setTextColor(Color.parseColor("#8899A6"))
            setPadding(0, d(8f), 0, 0)
        }, lp())

        // ---------------- 面板布局 ----------------
        root.addView(sectionTitle("面板布局"))
        val tuneCtx = activity.applicationContext

        fun addTuningSlider(label: String, key: String, min: Int, max: Int, cur: Int,
                            fmt: (Int) -> String) {
            val lab = TextView(activity).apply {
                textSize = 12f
                setTextColor(Color.WHITE)
                text = "$label：${fmt(cur)}"
            }
            val sk = SeekBar(activity).apply {
                this.max = (max - min).coerceAtLeast(1)
                progress = (cur - min).coerceIn(0, max - min)
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(seekBar: SeekBar?, value: Int, fromUser: Boolean) {
                        val real = value + min
                        lab.text = "$label：${fmt(real)}"
                        PanelTuning.set(tuneCtx, key, real)
                        OverlayLayout.current?.reapplyTuning()
                    }
                    override fun onStartTrackingTouch(seekBar: SeekBar?) {}
                    override fun onStopTrackingTouch(seekBar: SeekBar?) {}
                })
            }
            root.addView(lab, lp())
            root.addView(sk, lp())
        }

        addTuningSlider("内边距", PanelTuning.KEY_PADDING, 0, 40,
            PanelTuning.padding(tuneCtx)) { "${it}dp" }
        addTuningSlider("表头高度", PanelTuning.KEY_HEADER_H, 0, 120,
            PanelTuning.headerH(tuneCtx)) { if (it == 0) "自适应" else "${it}dp" }
        addTuningSlider("分隔线", PanelTuning.KEY_DIVIDER_H, 0, 12,
            PanelTuning.dividerH(tuneCtx)) { if (it == 0) "隐藏" else "${it}dp" }
        addTuningSlider("主体高度", PanelTuning.KEY_BODY_H, 0, 900,
            PanelTuning.bodyH(tuneCtx)) { if (it == 0) "自适应(以左栏为准)" else "${it}dp" }
        addTuningSlider("分类图标大小", PanelTuning.KEY_ICON_DP, 12, 48,
            PanelTuning.iconDp(tuneCtx)) { "${it}dp" }

        root.addView(TextView(activity).apply {
            text = "拖动即时生效（面板开着也生效）并记住。主体高度=0 时以左栏为准。"
            textSize = 11f
            setTextColor(Color.parseColor("#8899A6"))
            setPadding(0, d(8f), 0, 0)
        }, lp())

        // ---------------- 检查更新 ----------------
        root.addView(sectionTitle("检查更新"))
        root.addView(TextView(activity).apply {
            text = "当前版本：${com.kael.texinject.Updater.currentVersion(activity)}\n来源：github.com/${com.kael.texinject.Updater.REPO}"
            textSize = 11.5f
            setTextColor(Color.parseColor("#8899A6"))
            setPadding(0, d(4f), 0, d(8f))
        }, lp())
        root.addView(TextView(activity).apply {
            text = "立即检查更新"
            textSize = 13f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            setPadding(0, d(10f), 0, d(10f))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#CC0A84FF"))
                cornerRadius = d(10f).toFloat()
            }
            isClickable = true
            setOnClickListener { com.kael.texinject.UpdateUi.checkAndShow(activity, false) }
        }, lp())

        // ---------------- 主界面文字 ----------------
        root.addView(sectionTitle("主界面文字"))
        val quoteInput = EditText(activity).apply {
            hint = Brand.DEFAULT_QUOTE
            setText(Brand.quote(activity))
            setTextColor(Color.WHITE)
            setHintTextColor(Color.parseColor("#FF66707A"))
            textSize = 13f
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#33FFFFFF"))
                cornerRadius = d(10f).toFloat()
            }
            setPadding(d(12f), d(10f), d(12f), d(10f))
            setSingleLine(true)
        }
        quoteInput.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
            override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
            override fun afterTextChanged(s: android.text.Editable?) {
                Brand.setQuote(activity, s?.toString() ?: "")
            }
        })
        root.addView(quoteInput, lp())
        root.addView(TextView(activity).apply {
            text = "显示在面板标题「TexInject」后面，留空则只显示 TexInject。"
            textSize = 11f
            setTextColor(Color.parseColor("#8899A6"))
            setPadding(0, d(6f), 0, 0)
        }, lp())

        // ---------------- 打开自定义网站 ----------------
        root.addView(sectionTitle("打开自定义网站"))
        val webPrefs = activity.getSharedPreferences("texinject_website", Context.MODE_PRIVATE)
        val urlInput = EditText(activity).apply {
            hint = "输入网址，例如 www.example.com"
            setText(webPrefs.getString("url", ""))
            setTextColor(Color.WHITE)
            setHintTextColor(Color.parseColor("#FF66707A"))
            textSize = 13f
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#33FFFFFF"))
                cornerRadius = d(10f).toFloat()
            }
            setPadding(d(12f), d(10f), d(12f), d(10f))
            setSingleLine(true)
        }
        root.addView(urlInput, lp())
        root.addView(TextView(activity).apply {
            text = "打开"
            textSize = 13f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            setPadding(0, d(10f), 0, d(10f))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#CC0A84FF"))
                cornerRadius = d(10f).toFloat()
            }
            isClickable = true
            setOnClickListener {
                val u = urlInput.text.toString().trim()
                if (u.isNotEmpty()) webPrefs.edit().putString("url", u).apply()
                WebsiteFloatingWindow.show(activity, u)
            }
        }, lp().apply { topMargin = d(6f) })
        val widthLbl = TextView(activity).apply {
            text = "窗口宽度：${WebsiteFloatingWindow.widthPercent(activity).toInt()}%"
            textSize = 12f
            setTextColor(Color.WHITE)
        }
        root.addView(widthLbl, lp())
        root.addView(SeekBar(activity).apply {
            max = 80
            progress = (WebsiteFloatingWindow.widthPercent(activity) - 20).toInt()
            progressTintList = android.content.res.ColorStateList.valueOf(Color.parseColor("#CC0A84FF"))
            setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, v: Int, fromUser: Boolean) {
                    if (fromUser) widthLbl.text = "窗口宽度：${v + 20}%"
                }
                override fun onStartTrackingTouch(s: SeekBar) {}
                override fun onStopTrackingTouch(s: SeekBar) {
                    WebsiteFloatingWindow.setWidthPercent(activity, (s.progress + 20).toFloat())
                }
            })
        }, lp())
        val heightLbl = TextView(activity).apply {
            text = "窗口高度：${WebsiteFloatingWindow.heightPercent(activity).toInt()}%"
            textSize = 12f
            setTextColor(Color.WHITE)
        }
        root.addView(heightLbl, lp())
        root.addView(SeekBar(activity).apply {
            max = 80
            progress = (WebsiteFloatingWindow.heightPercent(activity) - 20).toInt()
            progressTintList = android.content.res.ColorStateList.valueOf(Color.parseColor("#CC0A84FF"))
            setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, v: Int, fromUser: Boolean) {
                    if (fromUser) heightLbl.text = "窗口高度：${v + 20}%"
                }
                override fun onStartTrackingTouch(s: SeekBar) {}
                override fun onStopTrackingTouch(s: SeekBar) {
                    WebsiteFloatingWindow.setHeightPercent(activity, (s.progress + 20).toFloat())
                }
            })
        }, lp())

        // ---------------- 声明 ----------------
        root.addView(sectionTitle("声明"))
        root.addView(TextView(activity).apply {
            text = "材质包注入、视频替换功能来自 Kusug UI（作者：bi匕匕bi）。"
            textSize = 12f
            setTextColor(Color.WHITE)
            setPadding(0, 0, 0, d(10f))
        }, lp())

        // ---------------- 模块说明（最底部） ----------------
        root.addView(sectionTitle("模块说明"))
        root.addView(TextView(activity).apply {
            text = "TexInject · Minecraft 中国版（网易版）增强面板\n" +
                "适配游戏版本：3.9.15.297907\n" +
                "框架：LSPosed（现代 LibXposed API 102） · 仅 arm64-v8a\n\n" +
                "分类说明：\n" +
                "  材质替换 — 材质包注入 / 恢复官方 / 主页视频替换 / 重载外观\n" +
                "  音乐 — 网易云在线播放 / 扫码登录 / 歌词 / 播放条\n" +
                "  脚本 — .py 直接执行、.mcp 注册加载\n" +
                "  快捷键 — MCP 模块开关与其悬浮胶囊\n" +
                "  设置 — 播放模式 / 播放条尺寸 / 面板布局 / 分类图标大小\n\n" +
                "自定义素材目录（图标/背景/视频/音乐条）：\n" +
                "  Android/data/com.netease.x19/files/pack_netease/icon/\n\n" +
                "许可：GNU AGPL-3.0-or-later\n" +
                "源码：https://github.com/Devout9527/TexInject"
            textSize = 11.5f
            setTextColor(Color.parseColor("#8899A6"))
            setLineSpacing(d(4f).toFloat(), 1f)
            setPadding(0, d(6f), 0, d(20f))
        }, lp())

        return root
    }

    private fun lp() = LinearLayout.LayoutParams(
        LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
    )
}