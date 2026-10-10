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
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.text.TextUtils
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.PopupWindow
import android.widget.TextView

/**
 * 自定义网站悬浮窗（单个 PopupWindow，内部切 full/mini 视图）。
 * - 尺寸（宽/高占屏百分比）在 设置 里自定义
 * - 顶栏左上角圆圈 -> 缩成小方块（favicon），点方块展开
 * - 顶栏标题长按 0.5 秒拖动整窗；小方块长按拖动
 * - 右上角 × -> 关闭
 */
object WebsiteFloatingWindow {
    private const val PREFS = "texinject_website"
    private var popup: PopupWindow? = null
    private var web: WebView? = null
    private var iconView: ImageView? = null
    private var favicon: Bitmap? = null
    private var activityRef: Activity? = null
    private var fullW = 0; private var fullH = 0
    private var fullX = 0; private var fullY = 0
    private var miniX = 0; private var miniY = 0
    private var miniSize = 0
    private var initedFull = false; private var initedMini = false
    private var fullView: View? = null
    private var miniView: View? = null
    private var collapsed = false
    private val main = Handler(Looper.getMainLooper())

    private fun prefs(c: Context) = c.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    fun widthPercent(c: Context) = prefs(c).getFloat("w", 38f).coerceIn(20f, 100f)
    fun heightPercent(c: Context) = prefs(c).getFloat("h", 88f).coerceIn(20f, 100f)
    fun setWidthPercent(c: Context, v: Float) { prefs(c).edit().putFloat("w", v.coerceIn(20f, 100f)).apply() }
    fun setHeightPercent(c: Context, v: Float) { prefs(c).edit().putFloat("h", v.coerceIn(20f, 100f)).apply() }

    fun show(activity: Activity, rawUrl: String) {
        var url = rawUrl.trim()
        if (url.isEmpty()) return
        if (!url.startsWith("http://") && !url.startsWith("https://")) url = "https://$url"
        activityRef = activity
        hide()
        main.post { build(activity, url) }
    }

    fun collapse() {
        main.post {
            collapsed = true
            fullView?.visibility = View.GONE
            miniView?.visibility = View.VISIBLE
            popup?.let { runCatching { it.update(miniX, miniY, miniSize, miniSize) } }
        }
    }

    fun expand() {
        main.post {
            collapsed = false
            miniView?.visibility = View.GONE
            fullView?.visibility = View.VISIBLE
            popup?.let { runCatching { it.update(fullX, fullY, fullW, fullH) } }
        }
    }

    fun hide() {
        main.post {
            popup?.let { runCatching { it.dismiss() } }
            web?.let { runCatching { it.onPause(); it.destroy() } }
            popup = null; web = null; fullView = null; miniView = null
            iconView = null; favicon = null; activityRef = null
            collapsed = false; initedFull = false; initedMini = false
        }
    }

    private fun build(activity: Activity, url: String) {
        val dm = activity.resources.displayMetrics
        fun dp(v: Float) = (v * dm.density + .5f).toInt()
        fullW = (dm.widthPixels * widthPercent(activity) / 100f).toInt()
        fullH = (dm.heightPixels * heightPercent(activity) / 100f).toInt()
        miniSize = dp(40f)
        if (!initedFull) { fullX = (dm.widthPixels - fullW) / 2; fullY = (dm.heightPixels - fullH) / 2; initedFull = true }
        if (!initedMini) { miniX = dm.widthPixels - miniSize - dp(12f); miniY = dm.heightPixels / 2 - miniSize / 2; initedMini = true }

        val slop = ViewConfiguration.get(activity).scaledTouchSlop

        // ---------- fullView ----------
        val topBar = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(8f), dp(6f), dp(8f), dp(6f))
            background = ColorDrawable(Color.parseColor("#FF26282C"))
        }
        val collapseBtn = TextView(activity).apply {
            text = "●"; textSize = 13f; gravity = Gravity.CENTER; setTextColor(Color.WHITE)
            background = GradientDrawable().apply { setColor(Color.parseColor("#33FFFFFF")); setShape(GradientDrawable.OVAL) }
            isClickable = true
            layoutParams = LinearLayout.LayoutParams(dp(26f), dp(26f))
            setOnTouchListener { v, ev ->
                if (ev.actionMasked == MotionEvent.ACTION_UP) { WebsiteFloatingWindow.collapse(); v.performClick() }
                true
            }
        }
        val title = TextView(activity).apply {
            text = url; setTextColor(Color.WHITE); textSize = 12f; maxLines = 1; ellipsize = TextUtils.TruncateAt.END
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply { leftMargin = dp(8f) }
        }
        val close = TextView(activity).apply {
            text = "×"; textSize = 17f; gravity = Gravity.CENTER; setTextColor(Color.WHITE)
            background = GradientDrawable().apply { setColor(Color.parseColor("#33FFFFFF")); cornerRadius = dp(13f).toFloat() }
            isClickable = true
            layoutParams = LinearLayout.LayoutParams(dp(26f), dp(26f)).apply { leftMargin = dp(8f) }
            setOnTouchListener { v, ev ->
                if (ev.actionMasked == MotionEvent.ACTION_UP) { WebsiteFloatingWindow.hide(); v.performClick() }
                true
            }
        }
        topBar.addView(collapseBtn); topBar.addView(title); topBar.addView(close)

        val wv = WebView(activity).apply {
            settings.javaScriptEnabled = true; settings.domStorageEnabled = true
            settings.useWideViewPort = true; settings.loadWithOverviewMode = true
            webViewClient = WebViewClient()
            webChromeClient = object : WebChromeClient() {
                override fun onReceivedIcon(view: WebView, icon: Bitmap) {
                    WebsiteFloatingWindow.favicon = icon
                    main.post { iconView?.setImageBitmap(icon) }
                }
            }
            loadUrl(url)
        }
        web = wv

        val full = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; visibility = View.VISIBLE }
        full.addView(topBar, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        full.addView(wv, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        fullView = full

        // 标题长按 0.5 秒拖动
        val hDrag = Handler(Looper.getMainLooper())
        var dPid = -1; var dragging = false; var dx = 0f; var dy = 0f; var sx = 0; var sy = 0
        val press = Runnable { dragging = true }
        title.setOnTouchListener { _, ev ->
            val idx = ev.actionIndex
            val pid = if (idx in 0 until ev.pointerCount) ev.getPointerId(idx) else -1
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (dPid == -1 && pid >= 0) { dPid = pid; dragging = false; dx = ev.rawX; dy = ev.rawY; sx = fullX; sy = fullY; hDrag.postDelayed(press, 500) }
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (pid == dPid) {
                        if (dragging) { fullX = sx + (ev.rawX - dx).toInt(); fullY = sy + (ev.rawY - dy).toInt(); popup?.let { runCatching { it.update(fullX, fullY, -1, -1) } } }
                        else if (Math.hypot((ev.rawX - dx).toDouble(), (ev.rawY - dy).toDouble()) > slop) hDrag.removeCallbacks(press)
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    if (ev.actionMasked == MotionEvent.ACTION_CANCEL || pid == dPid) { hDrag.removeCallbacks(press); dragging = false; dPid = -1 }
                    true
                }
                else -> true
            }
        }

        // ---------- miniView ----------
        val box = FrameLayout(activity).apply {
            background = GradientDrawable().apply { setColor(Color.parseColor("#FF1E1E24")); setShape(GradientDrawable.OVAL); setStroke(dp(1f), Color.parseColor("#33FFFFFF")) }
            visibility = View.GONE
        }
        val iv = ImageView(activity).apply { scaleType = ImageView.ScaleType.CENTER_CROP; layoutParams = FrameLayout.LayoutParams(-1, -1) }
        iconView = iv
        box.addView(iv)
        miniView = box

        // 小方块：长按 0.45 秒拖动；点一下展开
        val hMini = Handler(Looper.getMainLooper())
        var mPid = -1; var mDrag = false; var mx = 0f; var my = 0f; var msx = 0; var msy = 0
        val mPress = Runnable { mDrag = true }
        box.setOnTouchListener { _, ev ->
            val idx = ev.actionIndex
            val pid = if (idx in 0 until ev.pointerCount) ev.getPointerId(idx) else -1
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (mPid == -1 && pid >= 0) { mPid = pid; mDrag = false; mx = ev.rawX; my = ev.rawY; msx = miniX; msy = miniY; hMini.postDelayed(mPress, 450) }
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (pid == mPid) {
                        if (mDrag) { miniX = msx + (ev.rawX - mx).toInt(); miniY = msy + (ev.rawY - my).toInt(); popup?.let { runCatching { it.update(miniX, miniY, -1, -1) } } }
                        else if (Math.hypot((ev.rawX - mx).toDouble(), (ev.rawY - my).toDouble()) > slop) hMini.removeCallbacks(mPress)
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    if (ev.actionMasked == MotionEvent.ACTION_CANCEL || pid == mPid) {
                        hMini.removeCallbacks(mPress); val was = mDrag; mDrag = false; mPid = -1
                        if (!was && ev.actionMasked != MotionEvent.ACTION_CANCEL) WebsiteFloatingWindow.expand()
                    }
                    true
                }
                else -> true
            }
        }

        // ---------- 容器 + Popup ----------
        val container = FrameLayout(activity).apply {
            background = GradientDrawable().apply { setColor(Color.parseColor("#FF1A1C1E")); cornerRadius = dp(14f).toFloat(); setStroke(dp(1f), Color.parseColor("#33FFFFFF")) }
        }
        container.addView(full, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        container.addView(box, FrameLayout.LayoutParams(miniSize, miniSize, Gravity.CENTER))

        val pw = PopupWindow(container, fullW, fullH, false)
        pw.isTouchable = true; pw.isFocusable = false; pw.isOutsideTouchable = false
        pw.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        runCatching { pw.showAtLocation(activity.window.decorView, Gravity.TOP or Gravity.START, fullX, fullY) }
        popup = pw
    }
}
