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
import android.graphics.Outline
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Process
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewOutlineProvider
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import com.kael.texinject.GamePaths
import com.kael.texinject.LoginVideo
import com.kael.texinject.TexInjector
import com.kael.texinject.MusicPlayer

/**
 * 游戏内悬浮窗：可拖动悬浮球 + 侧边栏式设置面板（复刻 CreeperBox ClickGUI 的结构与配色）。
 *
 * 结构：主面板（深色圆角 #C0252525）→ 头部标题 + 分隔线 + 左侧分类栏 + 右侧内容卡片。
 * 选中分类用蓝色 #FF0062FF 高亮。
 */
class OverlayLayout(
    private val activity: Activity,
    private val previewMode: Boolean = false
) : FrameLayout(activity) {

    private var floatingBall: FrameLayout? = null
    private var panelContainer: FrameLayout? = null
    private var panelOpen = false
    private var contentHost: LinearLayout? = null
    private var buildChipsRow: LinearLayout? = null
    private var mainScroll: com.kael.texinject.ui.MaxScrollView? = null
    private var sidebarScrollRef: com.kael.texinject.ui.MaxScrollView? = null
    private var rightColumn: LinearLayout? = null
    private var bodyRow: LinearLayout? = null
    private var panelContent: LinearLayout? = null
    private var panelHeader: LinearLayout? = null
    private var panelDivider: View? = null
    private var contentCapH = 0
    private var musicHost: LinearLayout? = null
    private var statusText: TextView? = null
    private var cardHost: FrameLayout? = null
    private var cardBgImage: ImageView? = null
    private var cardMediaHost: FrameLayout? = null
    private var panelVideo: com.kael.texinject.ui.CenterCropVideo? = null
    private val sidebarItems = mutableListOf<View>()
    private val sidebarLabels = mutableListOf<TextView>()
    private val sidebarIconViews = mutableListOf<ImageView>()
    private var selectedCat = 0

    private val prefs = activity.getSharedPreferences("texinject_overlay", Context.MODE_PRIVATE)
    private var posX = 0
    private var posY = 0
    private val dark = UiTheme.isDarkMode(activity)
    private val cats = listOf("材质替换", "音乐", "脚本", "快捷键", "设置")
    @Volatile private var ballDragging = false

    private fun dp(v: Float) = (v * resources.displayMetrics.density + .5f).toInt()

    /** 应用「面板布局」自定义项：内边距 / 表头高度 / 分隔线 / 主体高度。 */
    private fun applyPanelTuning() {
        val pad = dp(PanelTuning.padding(activity).toFloat())
        panelContent?.setPadding(pad, pad, pad, pad)

        panelHeader?.let { h ->
            val hh = PanelTuning.headerH(activity)
            (h.layoutParams as? LinearLayout.LayoutParams)?.let { lp ->
                lp.height = if (hh > 0) dp(hh.toFloat()) else LayoutParams.WRAP_CONTENT
                h.layoutParams = lp
            }
            h.requestLayout()
        }
        panelDivider?.let { dv ->
            val dh = PanelTuning.dividerH(activity)
            dv.visibility = if (dh <= 0) View.GONE else View.VISIBLE
            (dv.layoutParams as? LinearLayout.LayoutParams)?.let { lp ->
                lp.height = dp(dh.coerceAtLeast(1).toFloat())
                dv.layoutParams = lp
            }
            dv.requestLayout()
        }
        bodyRow?.let { b ->
            val bh = PanelTuning.bodyH(activity)
            (b.layoutParams as? LinearLayout.LayoutParams)?.let { lp ->
                lp.height = if (bh > 0) dp(bh.toFloat()) else LayoutParams.WRAP_CONTENT
                b.layoutParams = lp
            }
            b.requestLayout()
        }

        // 分类图标大小
        val iconPx = dp(PanelTuning.iconDp(activity).toFloat())
        for (iv in sidebarIconViews) {
            (iv.layoutParams as? LinearLayout.LayoutParams)?.let { lp ->
                lp.width = iconPx
                lp.height = iconPx
                iv.layoutParams = lp
            }
        }

        // 每个分类项固定高度 = 主体高度 / 分类数（不随图标变化，正好铺满 → 不滚动）
        val bhDp = PanelTuning.bodyH(activity)
        if (bhDp > 0 && sidebarItems.isNotEmpty()) {
            val itemH = dp(bhDp.toFloat()) / sidebarItems.size
            for (it in sidebarItems) {
                (it.layoutParams as? LinearLayout.LayoutParams)?.let { lp ->
                    lp.height = itemH
                    lp.bottomMargin = 0
                    it.layoutParams = lp
                }
            }
        }
    }

    /** 供「设置」里改完滑块后即时刷新。 */
    fun reapplyTuning() {
        applyPanelTuning()
    }

    companion object {
        @Volatile
        var current: OverlayLayout? = null
    }

    init {
        current = this
        layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
        isClickable = false
        clipChildren = false
        clipToPadding = false
        if (previewMode) {
            // 桌面入口：只渲染面板布局，不挂悬浮球/悬浮窗/任何注入功能
            buildPanel()
            showPanel()
        } else {
            buildFloatingBall()
            buildPanel()
            NowPlayingBar.attach(activity, this) // 点击不再打开主页面（只保留长按拖动）
        }
    }

    // ---------------- 悬浮球 ----------------

    private fun buildFloatingBall() {
        val size = dp(40f)
        val ball = FrameLayout(activity).apply {
            layoutParams = LayoutParams(size, size, Gravity.TOP or Gravity.START).apply {
                leftMargin = dp(16f); topMargin = dp(64f)
            }
            clipChildren = false
            clipToPadding = false
            background = rounded(Color.parseColor("#CC1E1E22"), dp(12f).toFloat())
            elevation = dp(4f).toFloat()
            // 圆角裁剪（图标按圆角显示）
            clipToOutline = true
            outlineProvider = object : ViewOutlineProvider() {
                override fun getOutline(view: View, outline: Outline) {
                    outline.setRoundRect(0, 0, view.width, view.height, dp(12f).toFloat())
                }
            }
        }
        val videoFile = IconLoader.findBallVideo(activity, GamePaths.sourceDir)
        if (videoFile != null) {
            // 悬浮球视频：和播放条同一套 CenterCropVideo（居中裁剪铺满整球，不拉伸变形）
            val cv = CenterCropVideo(activity)
            cv.attach(ball)
            cv.play(videoFile.absolutePath)
        } else {
            val icon = ImageView(activity).apply {
                layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
                setImageDrawable(IconLoader.getLogoDrawable(activity, GamePaths.sourceDir))
                scaleType = ImageView.ScaleType.FIT_CENTER
            }
            ball.addView(icon)
        }
        setupBallDrag(ball)
        ball.setOnClickListener { togglePanel() }
        addView(ball)
        floatingBall = ball
        if (prefs.contains("x") && prefs.contains("y")) {
            ball.post { posX = prefs.getInt("x", dp(16f)); posY = prefs.getInt("y", dp(64f)); applyBallPos() }
        }
    }

    private fun applyBallPos() {
        val ball = floatingBall ?: return
        (ball.layoutParams as? LayoutParams)?.let {
            it.gravity = Gravity.TOP or Gravity.START
            it.leftMargin = posX; it.topMargin = posY
            ball.layoutParams = it
        }
    }

    /** 长按约 1 秒进入拖动（带抖动容差），松手前只点击不拖。 */
    private fun setupBallDrag(ball: View) {
        val enterDrag = Runnable {
            ballDragging = true
            ball.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        }
        val tol = resources.displayMetrics.density * 26f
        ball.setOnTouchListener(object : OnTouchListener {
            private var downRawX = 0f; private var downRawY = 0f
            private var startX = 0; private var startY = 0
            private var moved = false

            override fun onTouch(v: View, e: MotionEvent): Boolean {
                when (e.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        downRawX = e.rawX; downRawY = e.rawY; startX = posX; startY = posY
                        moved = false; v.isPressed = true
                        ballDragging = false
                        v.parent?.requestDisallowInterceptTouchEvent(true)
                        v.removeCallbacks(enterDrag)
                        v.postDelayed(enterDrag, 1000L)
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val dx = e.rawX - downRawX; val dy = e.rawY - downRawY
                        if (!ballDragging && dx * dx + dy * dy > tol * tol) {
                            moved = true
                            v.removeCallbacks(enterDrag)
                        }
                        if (ballDragging) {
                            posX = (startX + dx).toInt().coerceAtLeast(0)
                            posY = (startY + dy).toInt().coerceAtLeast(0)
                            applyBallPos()
                        }
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        v.removeCallbacks(enterDrag)
                        v.isPressed = false
                        if (ballDragging) {
                            prefs.edit().putInt("x", posX).putInt("y", posY).apply()
                            ballDragging = false
                        } else if (e.actionMasked == MotionEvent.ACTION_UP && !moved) {
                            v.performClick()
                        }
                    }
                }
                return true
            }
        })
    }

    // ---------------- 面板 ----------------

    private fun buildPanel() {
        val container = FrameLayout(activity).apply {
            layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
            isClickable = true
            visibility = View.GONE
        }
        container.addView(View(activity).apply {
            layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
            setBackgroundColor(Color.TRANSPARENT)
            setOnClickListener { hidePanel() }
        })

        val width = minOf(dp(540f), (resources.displayMetrics.widthPixels * 0.94f).toInt())
        val radius = dp(22f).toFloat()
        val panelMaxH = (resources.displayMetrics.heightPixels * 0.82f).toInt()

        val card = FrameLayout(activity).apply {
            layoutParams = LayoutParams(width, LayoutParams.WRAP_CONTENT, Gravity.CENTER)
            background = rounded(UiTheme.cbPanel, radius)
            elevation = dp(10f).toFloat()
            clipToOutline = true
            outlineProvider = object : ViewOutlineProvider() {
                override fun getOutline(view: View, outline: Outline) {
                    outline.setRoundRect(0, 0, view.width, view.height, radius)
                }
            }
            isClickable = true
        }
        cardHost = card
        // 卡片媒体层（背景图/背景视频，只铺这张卡片）
        val media = FrameLayout(activity).apply {
            layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
        }
        card.addView(media)
        cardMediaHost = media
        val cardBg = ImageView(activity).apply {
            layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
            scaleType = ImageView.ScaleType.CENTER_CROP
            visibility = View.GONE
        }
        media.addView(cardBg)
        cardBgImage = cardBg

        val content = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT)
            setPadding(dp(16f), dp(14f), dp(16f), dp(16f))
        }
        card.addView(content)
        panelContent = content

        // 头部
        val header = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val logoBox = FrameLayout(activity).apply {
            val s = dp(24f)
            layoutParams = LinearLayout.LayoutParams(s, s).apply { rightMargin = dp(8f) }
            clipToOutline = true
            outlineProvider = object : ViewOutlineProvider() {
                override fun getOutline(view: View, outline: Outline) {
                    outline.setRoundRect(0, 0, view.width, view.height, dp(6f).toFloat())
                }
            }
        }
        val logoVideo = IconLoader.findLogoVideo(activity, GamePaths.sourceDir)
        if (logoVideo != null) {
            // 左上角图标支持视频：与悬浮球/播放条同一套居中裁剪
            val cv = CenterCropVideo(activity)
            cv.attach(logoBox)
            cv.play(logoVideo.absolutePath)
        } else {
            logoBox.addView(ImageView(activity).apply {
                layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
                setImageDrawable(IconLoader.getLogoDrawable(activity, GamePaths.sourceDir))
                scaleType = ImageView.ScaleType.FIT_CENTER
            })
        }
        header.addView(logoBox)
        header.addView(TextView(activity).apply {
            text = Brand.title(activity)
            textSize = 15f
            maxLines = 1
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setTextColor(Color.WHITE)
            layoutParams = LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1f)
        })
        header.addView(TextView(activity).apply {
            text = "×"
            gravity = Gravity.CENTER
            textSize = 17f
            setTextColor(Color.WHITE)
            background = rounded(UiTheme.cbList, dp(7f).toFloat())
            isClickable = true
            setOnClickListener { hidePanel() }
            layoutParams = LinearLayout.LayoutParams(dp(26f), dp(26f))
        })
        content.addView(header, wrap())
        panelHeader = header

        // 分隔线
        val dividerView = View(activity).apply {
            setBackgroundColor(UiTheme.cbDivider)
        }
        content.addView(dividerView, LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, dp(1f)).apply { topMargin = dp(10f); bottomMargin = dp(10f) })
        panelDivider = dividerView

        // 主体：左分类栏 + 右内容
        val body = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        bodyRow = body

        val sidebar = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            // 背景(白圈)在外层固定框上，这里透明，只负责滚动内容
        }
        sidebarItems.clear()
        sidebarLabels.clear()
        cats.forEachIndexed { i, name ->
            // 叠层容器：图标在下层，文字盖在上层（Z 轴）
            val item = FrameLayout(activity).apply {
                isClickable = true
                setOnClickListener { renderCategory(i) }
            }
            // 下层：图标（居中）。自定义 PNG 优先，否则用内置矢量图标
            val bmp = CategoryIcons.get(activity, GamePaths.sourceDir, name, i)
            val builtin = CategoryIcons.builtinRes(i)
            if (bmp != null || builtin != null) {
                // 图标大小 = 「设置 → 面板布局 → 分类图标大小」(dp)，可自定义
                val side = dp(PanelTuning.iconDp(activity).toFloat())
                val iv = ImageView(activity).apply {
                    layoutParams = FrameLayout.LayoutParams(side, side, Gravity.CENTER)
                    if (bmp != null) {
                        setImageBitmap(bmp)
                    } else {
                        // 关键：用模块自己的 Resources，否则游戏内解析不到这个 id
                        val d = com.kael.texinject.ui.ModuleRes.drawable(activity, builtin!!)
                        setImageDrawable(d)
                        imageTintList = android.content.res.ColorStateList.valueOf(Color.WHITE)
                    }
                    scaleType = ImageView.ScaleType.FIT_CENTER
                }
                com.kael.texinject.TexInjectHookInit.logExternal(
                    "[TexInject] cat icon[$i] name=$name " +
                    (if (bmp != null) "custom bitmap" else "builtin res=$builtin") +
                    " size=${side}px"
                )
                sidebarIconViews.add(iv)
                item.addView(iv)
            }
            // 上层：文字（铺满整格、居中，压在图标上面）
            val label = TextView(activity).apply {
                text = name
                gravity = Gravity.CENTER
                textSize = 11f
                typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
                setTextColor(UiTheme.glassWhiteText)
                layoutParams = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )
            }
            sidebarLabels.add(label)
            item.addView(label)
            sidebarItems.add(item)
            sidebar.addView(item, LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT))
        }
        // 内容区高度上限（面板封顶 panelMaxH，预留表头/分隔线/状态/内边距）
        val contentH = (panelMaxH - dp(150f)).coerceAtLeast(dp(220f))
        contentCapH = contentH

        // 左侧分类：外层是固定的白圈框，里面 MaxScrollView 滚动
        val sidebarFrame = FrameLayout(activity).apply {
            background = GradientDrawable().apply { // 毛玻璃白（固定，不随内容滚动）
                setColor(UiTheme.glassWhite)
                cornerRadius = dp(12f).toFloat()
                setStroke(dp(1f), UiTheme.glassWhiteBorder)
            }
            setPadding(dp(6f), dp(6f), dp(6f), dp(6f))
        }
        val sidebarScroll = MaxScrollView(activity, contentH).apply {
            addView(sidebar, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT))
        }
        sidebarFrame.addView(
            sidebarScroll,
            FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
        )
        body.addView(sidebarFrame, LinearLayout.LayoutParams(dp(76f), LinearLayout.LayoutParams.MATCH_PARENT))
        sidebarScrollRef = sidebarScroll

        val right = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply { leftMargin = dp(10f) }
        }
        rightColumn = right
        statusText = TextView(activity).apply {
            textSize = 11f
            setTextColor(Color.parseColor("#A9A9A9"))
            setLineSpacing(1.5f, 1f)
            setPadding(dp(2f), dp(2f), 0, dp(8f))
        }
        right.addView(statusText, wrap())
        contentHost = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        // 建筑分类的 chips 行放右栏固定位置（滚动区之上），
        // 这样切到导出/投影（隐藏滚动区）时 chips 不会跟着消失。
        buildChipsRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            visibility = View.GONE
        }
        right.addView(buildChipsRow, wrap().apply { bottomMargin = dp(6f) })
        // 滚动区吃掉右栏剩余高度（权重铺满）；右栏高度 = 左栏高度
        val scroll = MaxScrollView(activity, contentH).apply {
            addView(
                contentHost,
                LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT)
            )
        }
        right.addView(scroll, LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f))
        mainScroll = scroll

        // 音乐固定容器：不进外层滚动，自身歌单可独立上下滑动
        musicHost = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }
        right.addView(musicHost, LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f))

        body.addView(right)
        content.addView(body, wrap())

        container.addView(card)
        // 快捷键悬浮层挂在主面板之下（早于 panelContainer 添加 → 主面板会盖住它）
        HotkeyLayer.attach(activity, this)
        FeatureHotkeys.init(activity)
        addView(container)
        panelContainer = container
        renderCategory(prefs.getInt("cat", 0).coerceIn(0, cats.size - 1))
    }

    private fun renderCategory(index: Int) {
        selectedCat = index
        prefs.edit().putInt("cat", index).apply()
        MusicPlayer.detachUi() // 只解绑界面，不停止播放（打开 UI 不打断音乐）
        sidebarItems.forEachIndexed { i, v ->
            v.background = if (i == index) rounded(UiTheme.cbHighlight, dp(9f).toFloat())
            else rounded(Color.TRANSPARENT, dp(9f).toFloat())
        }
        sidebarLabels.forEachIndexed { i, t ->
            t.setTextColor(if (i == index) Color.WHITE else UiTheme.glassWhiteText)
            // 选中 -> 只显示文字
            t.visibility = if (i == index) View.VISIBLE else View.GONE
        }
        sidebarIconViews.forEachIndexed { i, iv ->
            // 未选中 -> 只显示图标
            iv.visibility = if (i == index) View.GONE else View.VISIBLE
        }
        com.kael.texinject.TexInjectHookInit.logExternal(
            "[TexInject] renderCategory=$index icons=${sidebarIconViews.size} labels=${sidebarLabels.size}"
        )
        val host = contentHost ?: return
        host.removeAllViews()

        // 桌面预览：只显示布局占位，不加载任何分类功能
        if (previewMode) {
            host.addView(TextView(activity).apply {
                text = "预览：${cats.getOrElse(index) { "?" }} 分类\n" +
                    "桌面入口只显示面板布局，不加载任何注入功能。\n" +
                    "实际功能（材质替换/音乐/脚本/快捷键）在游戏内生效。"
                setTextColor(Color.parseColor("#FF9AA0A8"))
                textSize = 13f
                setPadding(dp(18f), dp(30f), dp(18f), 0)
            })
            return
        }

        // 音乐：走独立固定容器（不进外层滚动，歌单独立滑动）
        if (index == 1) {
            mainScroll?.visibility = View.GONE
            statusText?.visibility = View.GONE
            musicHost?.let { mh ->
                mh.visibility = View.VISIBLE
                mh.removeAllViews()
                mh.addView(MusicPlayer.buildView(activity))
            }
            return
        } else {
            mainScroll?.visibility = View.VISIBLE
            statusText?.visibility = View.VISIBLE
            musicHost?.visibility = View.GONE
        }

        when (index) {
            0 -> {
                addAction(host, "注入资源") { TexInjector.injectAll(activity, GamePaths.sourceDir).let { it.ok to it.summary } }
                addAction(host, "恢复官方") { TexInjector.restore(activity, GamePaths.sourceDir).let { it.ok to it.summary } }
                addAction(host, "替换视频") { LoginVideo.replace(activity, GamePaths.sourceDir).let { it.ok to it.summary } }
                addAction(host, "恢复视频") { LoginVideo.restore(activity).let { it.ok to it.summary } }
                addAction(host, "重载外观") { doReload(); true to "已重载外观" }
                addAction(host, "重启游戏") {
                    activity.runOnUiThread { hidePanel() }
                    android.os.Handler(android.os.Looper.getMainLooper())
                        .postDelayed({ Process.killProcess(Process.myPid()) }, 600)
                    true to "即将退出游戏"
                }
            }
            1 -> {
                host.addView(MusicPlayer.buildView(activity))
            }
            2 -> {
                host.addView(PyLoaderUi.buildView(activity))
            }
            3 -> {
                host.addView(HotkeyUi.buildView(activity))
            }
            4 -> {
                host.addView(SettingsUi.buildView(activity))
            }
        }
    }

    private fun addAction(host: LinearLayout, label: String, action: () -> Pair<Boolean, String>) {
        val card = TextView(activity).apply {
            text = label
            textSize = 14f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setTextColor(UiTheme.glassWhiteText)
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16f), dp(13f), dp(16f), dp(13f))
            background = GradientDrawable().apply { // 毛玻璃白
                setColor(UiTheme.glassWhite)
                cornerRadius = dp(12f).toFloat()
                setStroke(dp(1f), UiTheme.glassWhiteBorder)
            }
            isClickable = true
            setOnClickListener { runWork(label, action) }
        }
        host.addView(card, wrap().apply { bottomMargin = dp(8f) })
    }

    /** 重载所有可自定义的东西：图标 / 背景 / 视频 / 音乐条 / 悬浮球 / 面板布局。 */
    private fun doReload() {
        activity.runOnUiThread {
            IconLoader.clearCache()
            PanelBackground.clearCache()
            CategoryIcons.clearCache()
            com.kael.texinject.ui.NowPlayingBar.clearCache()
            com.kael.texinject.MusicPlayer.reloadCustomizations()
            refreshHeaderLogo()
            applyCardBackground()
            applyPanelTuning()
            renderCategory(selectedCat)
        }
    }

    /** 卡片背景：优先背景视频（循环静音），其次背景图，最后纯色。 */
    private fun applyCardBackground() {
        val card = cardHost ?: return
        val media = cardMediaHost ?: return
        val img = cardBgImage ?: return
        val video = PanelBackground.findVideo(activity, GamePaths.sourceDir)
        val custom = PanelBackground.get(activity, GamePaths.sourceDir)
        val pv = panelVideo ?: com.kael.texinject.ui.CenterCropVideo(activity).also { panelVideo = it }

        if (video != null) {
            img.visibility = View.GONE
            card.background = null
            pv.attach(media)
            pv.play(video.absolutePath) // 无进度条、循环静音、铺满卡片且被内容层覆盖
        } else {
            pv.hide()
            if (custom != null) {
                img.setImageBitmap(custom)
                img.visibility = View.VISIBLE
                card.background = null
            } else {
                img.visibility = View.GONE
                card.background = rounded(UiTheme.cbPanel, dp(22f).toFloat())
            }
        }
    }

    private fun refreshHeaderLogo() {
        // 头部/悬浮球图标刷新
        (floatingBall?.getChildAt(0) as? ImageView)?.setImageDrawable(
            IconLoader.getLogoDrawable(activity, GamePaths.sourceDir)
        )
    }

    private fun refreshStatus() {
        GamePaths.init(activity)
        val source = GamePaths.sourceDir
        val packs = TexInjector.scanPacks(source)
        val injected = TexInjector.targetHasContent()
        val hasVideo = LoginVideo.findSource(activity, source) != null
        val hasBg = PanelBackground.findFile(activity, source) != null
        statusText?.text = buildString {
            append("材质包 ").append(packs.size).append(" 个\n")
            append("槽位 ").append(if (injected) "已注入" else "空")
                .append(" · 视频 ").append(if (hasVideo) "就绪" else "无")
                .append(" · 背景 ").append(if (hasBg) "自定义" else "模糊")
        }
    }

    private fun runWork(action: String, block: () -> Pair<Boolean, String>) {
        statusText?.text = "$action…"
        Thread({
            val (_, msg) = block()
            activity.runOnUiThread {
                Toast.makeText(activity, msg, Toast.LENGTH_SHORT).show()
                refreshStatus()
            }
        }, "TexInject-Worker").start()
    }

    private fun togglePanel() { if (panelOpen) hidePanel() else showPanel() }

    private fun showPanel() {
        if (!previewMode) refreshStatus()
        if (!previewMode) applyCardBackground()
        renderCategory(selectedCat)
        if (!previewMode) applyPanelTuning()
        panelContainer?.visibility = View.VISIBLE
        panelOpen = true
    }

    private fun hidePanel() {
        panelContainer?.visibility = View.GONE
        panelOpen = false
    }

    // ---------------- 工具 ----------------

    private fun wrap() = LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT)

    private fun rounded(color: Int, radius: Float) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = radius
    }
}
