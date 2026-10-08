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
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Outline
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.text.TextUtils
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.view.ViewOutlineProvider
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File

/**
 * 右上角"正在播放"悬浮条：歌名/歌手 + 当前歌词行。
 *
 *   • 大小固定；文字超出宽度自动跑马灯滚动
 *   • 长按 3 秒进入拖动模式，可移动，位置会记住
 *   • 图标可自定义：music_icon.png / MusicIcon.png / 音乐图标.png / 播放图标.png
 *   • 背景层可自定义：
 *       图片 music_bar.png / 播放条.png / bar.png
 *       视频 music_bar.mp4 / 播放条.mp4（循环静音）
 */
object NowPlayingBar {
    private val findCache = HashMap<String, java.io.File?>()
    private val ICON_NAMES = listOf(
        "music_icon.png", "MusicIcon.png", "music_icon.jpg",
        "音乐图标.png", "播放图标.png", "music.png"
    )
    private val BAR_IMAGE_NAMES = listOf(
        "music_bar.png", "MusicBar.png", "musicbar.png", "bar.png", "播放条.png", "音乐条.png"
    )
    private val BAR_VIDEO_NAMES = listOf(
        "music_bar.mp4", "musicbar.mp4", "bar.mp4", "播放条.mp4", "音乐条.mp4",
        "music_bar.webm", "music_bar.mkv"
    )
    private const val LONG_PRESS_MS = 1000L

    private const val PREF_W = "bar_width_dp"
    private const val PREF_H = "bar_height_dp"
    private const val DEFAULT_W = 200
    private const val DEFAULT_H = 52
    const val MIN_W = 120
    const val MAX_W = 420
    const val MIN_H = 36
    const val MAX_H = 120

    private fun prefsOf(context: Context) =
        context.getSharedPreferences("texinject_nowplaying", Context.MODE_PRIVATE)

    fun getWidthDp(context: Context): Int =
        prefsOf(context).getInt(PREF_W, DEFAULT_W).coerceIn(MIN_W, MAX_W)

    fun getHeightDp(context: Context): Int =
        prefsOf(context).getInt(PREF_H, DEFAULT_H).coerceIn(MIN_H, MAX_H)

    /** 清掉自定义素材缓存（「重载外观」时调用）。 */
    fun clearCache() {
        findCache.clear()
    }

    /** 调整播放条尺寸(dp)，立即生效并记住。 */
    fun setSize(context: Context, widthDp: Int, heightDp: Int) {
        val w = widthDp.coerceIn(MIN_W, MAX_W)
        val h = heightDp.coerceIn(MIN_H, MAX_H)
        prefsOf(context).edit().putInt(PREF_W, w).putInt(PREF_H, h).apply()
        val box = root ?: return
        val density = box.resources.displayMetrics.density
        val lp = box.layoutParams ?: return
        lp.width = (w * density + .5f).toInt()
        lp.height = (h * density + .5f).toInt()
        box.layoutParams = lp
        box.requestLayout()
        box.invalidateOutline()
    }

    private var root: FrameLayout? = null
    private var songView: TextView? = null
    private var lyricView: TextView? = null
    private var prefs: android.content.SharedPreferences? = null
    private var appContext: android.content.Context? = null
    private val handler = Handler(Looper.getMainLooper())
    private var playIcon: ImageView? = null

    @Volatile
    private var dragging = false

    fun attach(activity: Activity, parent: ViewGroup, onTap: (() -> Unit)? = null) {
        appContext = activity.applicationContext
        if (root != null) return
        val dp = activity.resources.displayMetrics.density
        fun d(v: Float) = (v * dp + .5f).toInt()
        prefs = activity.getSharedPreferences("texinject_nowplaying", Context.MODE_PRIVATE)

        val w = d(getWidthDp(activity).toFloat())
        val h = d(getHeightDp(activity).toFloat())
        val radius = d(12f).toFloat()

        val box = FrameLayout(activity).apply {
            layoutParams = FrameLayout.LayoutParams(w, h, Gravity.TOP or Gravity.END).apply {
                topMargin = prefs?.getInt("top", d(46f)) ?: d(46f)
                rightMargin = prefs?.getInt("right", d(12f)) ?: d(12f)
            }
            elevation = d(6f).toFloat()
            visibility = View.GONE
            clipToOutline = true
            outlineProvider = object : ViewOutlineProvider() {
                override fun getOutline(v: View, outline: Outline) {
                    outline.setRoundRect(0, 0, v.width, v.height, radius)
                }
            }
        }

        // 先铺一层圆角底（保证圆角形状一定存在），视频/图片再叠上去
        box.background = GradientDrawable().apply {
            setColor(Color.parseColor("#D9121214"))
            cornerRadius = radius
        }

        // 背景层：视频 > 图片 > 默认深色
        val video = findFile(BAR_VIDEO_NAMES)
        val image = if (video == null) findFile(BAR_IMAGE_NAMES) else null
        if (video != null) {
            val cv = CenterCropVideo(activity)
            cv.attach(box)
            cv.play(video.absolutePath) // 居中裁剪铺满播放条，不拉伸变形
        } else if (image != null) {
            val iv = ImageView(activity).apply {
                layoutParams = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT
                )
                scaleType = ImageView.ScaleType.CENTER_CROP
                setImageBitmap(BitmapFactory.decodeFile(image.absolutePath))
            }
            box.addView(iv)
        }

        // 内容层（图标 + 歌名/歌词）
        val content = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(d(8f), d(4f), d(10f), d(4f))
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT
            )
        }
        val icon = ImageView(activity).apply {
            val s = d(30f)
            layoutParams = LinearLayout.LayoutParams(s, s).apply { rightMargin = d(7f) }
            scaleType = ImageView.ScaleType.FIT_CENTER
            // 播放条图标不支持自定义：暂停=三角 ▶，播放中="H 没有横"（两条竖杠）‖
            setImageResource(com.kael.texinject.R.drawable.ic_play_triangle)
        }
        playIcon = icon
        content.addView(icon)

        val col = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        songView = TextView(activity).apply {
            textSize = 12f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setTextColor(Color.WHITE)
            setSingleLine(true)
            ellipsize = TextUtils.TruncateAt.MARQUEE
            marqueeRepeatLimit = -1
            isSelected = true
        }
        lyricView = TextView(activity).apply {
            textSize = 11f
            setTextColor(Color.parseColor("#A9A9A9"))
            setSingleLine(true)
            ellipsize = TextUtils.TruncateAt.MARQUEE
            marqueeRepeatLimit = -1
            isSelected = true
        }
        col.addView(songView)
        col.addView(lyricView)
        content.addView(col)
        box.addView(content)

        setupDrag(activity, box, onTap)
        parent.addView(box)
        root = box
    }

    /** 长按 3 秒进入拖动，移动位置并记忆。 */
    private fun setupDrag(activity: Activity, box: View, onTap: (() -> Unit)?) {
        val slop = ViewConfiguration.get(activity).scaledTouchSlop
        val enterDrag = Runnable {
            dragging = true
            box.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        }
        box.setOnTouchListener(object : View.OnTouchListener {
            private var downRawX = 0f
            private var downRawY = 0f
            private var startTop = 0
            private var startRight = 0
            private var movedBeyondSlop = false
            // 允许一定抖动：超过 28dp 才判定为"正在拖"，避免长按过程中手抖把计时取消掉
            private val cancelTol = activity.resources.displayMetrics.density * 28f

            override fun onTouch(v: View, e: MotionEvent): Boolean {
                val lp = box.layoutParams as? FrameLayout.LayoutParams ?: return false
                when (e.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        downRawX = e.rawX; downRawY = e.rawY
                        startTop = lp.topMargin; startRight = lp.rightMargin
                        movedBeyondSlop = false
                        dragging = false
                        handler.removeCallbacks(enterDrag)
                        handler.postDelayed(enterDrag, LONG_PRESS_MS)
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val dx = e.rawX - downRawX
                        val dy = e.rawY - downRawY
                        if (!dragging && dx * dx + dy * dy > cancelTol * cancelTol) {
                            movedBeyondSlop = true
                            handler.removeCallbacks(enterDrag)
                        }
                        if (dragging) {
                            val maxTop = (box.parent as? View)?.height ?: 1080
                            lp.topMargin = (startTop + dy.toInt()).coerceIn(0, (maxTop - box.height).coerceAtLeast(0))
                            lp.rightMargin = (startRight - dx.toInt()).coerceIn(0, 1200)
                            box.layoutParams = lp
                        }
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        handler.removeCallbacks(enterDrag)
                        if (dragging) {
                            prefs?.edit()?.putInt("top", lp.topMargin)?.putInt("right", lp.rightMargin)?.apply()
                            dragging = false
                        } else if (e.actionMasked == MotionEvent.ACTION_UP && !movedBeyondSlop) {
                            // 单击 = 暂停/播放；双击 = 停止
                            val now = System.currentTimeMillis()
                            if (now - lastTapMs < 300L) {
                                handler.removeCallbacks(singleTapTask)
                                lastTapMs = 0L
                                try { com.kael.texinject.MusicPlayer.stop() } catch (e2: Throwable) {}
                                handler.postDelayed({ refreshIcon() }, 200L)
                            } else {
                                lastTapMs = now
                                handler.postDelayed(singleTapTask, 300L)
                            }
                            onTap?.invoke()
                        }
                    }
                }
                return true
            }
        })
    }

    private fun findFile(names: List<String>): File? {
        return try {
            val dirs = mutableListOf<File>()
            appContext?.let { dirs += com.kael.texinject.paths.BuildPaths.iconDir(it) }
            dirs += File("/storage/emulated/0/Android/data/com.netease.x19/files/pack_netease/icon")
            com.kael.texinject.GamePaths.sourceDir?.let { dirs += it }
            for (dir in dirs) {
                if (!dir.isDirectory) continue
                for (n in names) {
                    val f = File(dir, n)
                    if (f.isFile && f.length() > 0) return f
                }
            }
            null
        } catch (e: Throwable) { null }
    }

    private fun loadIcon(): android.graphics.Bitmap? = findFile(ICON_NAMES)?.let {
        try { BitmapFactory.decodeFile(it.absolutePath) } catch (e: Throwable) { null }
    }

    @Volatile private var lastTapMs = 0L

    private val singleTapTask = Runnable {
        try { com.kael.texinject.MusicPlayer.togglePlayPause() } catch (e: Throwable) {}
        handler.postDelayed({ refreshIcon() }, 150L)
    }

    /** 按当前播放状态刷新图标：暂停 -> ▶ ；播放中 -> ‖ */
    fun refreshIcon() {
        val playing = try { com.kael.texinject.MusicPlayer.isPlaying() } catch (e: Throwable) { false }
        val res = if (playing) com.kael.texinject.R.drawable.ic_pause_bars
                  else com.kael.texinject.R.drawable.ic_play_triangle
        playIcon?.setImageResource(res)
    }

    fun update(songText: String, lyricText: String) {
        songView?.text = songText
        lyricView?.text = lyricText
        songView?.isSelected = true
        lyricView?.isSelected = true
        root?.visibility = View.VISIBLE
    }

    fun hide() {
        root?.visibility = View.GONE
    }

    fun detach() {
        root = null
        songView = null
        lyricView = null
    }
}
