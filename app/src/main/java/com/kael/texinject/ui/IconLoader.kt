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

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import java.io.File

/**
 * 悬浮球图标加载器（可自定义）。
 *
 * 在若干目录里找**特殊命名**的图片，找到即用作悬浮球/面板图标：
 *   特殊文件名：Icon.png / icon.png / Logo.png / logo.png / Cover.png / cover.png
 *   搜索目录（按优先级）：
 *     1. 源目录（resource_packs）—— 和材质包放一起最方便
 *     2. <模块外部目录>/TexInject/Resources/
 *     3. <模块外部目录>/TexInject/
 *     4. <游戏外部目录>/TexInject/Resources/
 *     5. <游戏外部目录>/TexInject/
 * 全部找不到时，用一个内置生成的圆角默认图标。
 */
object IconLoader {
    private val SPECIAL_NAMES = listOf(
        "Icon.png", "icon.png", "Logo.png", "logo.png", "Cover.png", "cover.png"
    )

    /**
     * 悬浮球 / 面板图标共用的视频名（两个位置用同一份 —— 放一个就用一个）。
     * 找不到视频时回退到图片图标。
     */
    private val VIDEO_NAMES = listOf(
        "Ball.mp4", "ball.mp4", "球.mp4", "球视频.mp4",
        "Logo.mp4", "logo.mp4", "Icon.mp4", "icon.mp4",
        "Cover.mp4", "cover.mp4", "图标.mp4", "面板.mp4"
    )

    private fun findByName(context: Context, sourceDir: File?, names: List<String>): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in names) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    /** 悬浮球视频（和面板图标共用同一份）。 */
    fun findBallVideo(context: Context, sourceDir: File? = null): File? =
        findByName(context, sourceDir, VIDEO_NAMES)

    /** 面板左上角图标视频（和悬浮球共用同一份）。 */
    fun findLogoVideo(context: Context, sourceDir: File? = null): File? =
        findByName(context, sourceDir, VIDEO_NAMES)

    @Volatile
    private var cached: Bitmap? = null
    @Volatile
    private var cachedFrom: String? = null

    fun clearCache() {
        cached = null
        cachedFrom = null
    }

    private fun candidateDirs(context: Context, sourceDir: File?): List<File> {
        val list = mutableListOf<File>()
        // 自定义图标/视频只在 icon/ 等目录找，不再扫 resource_packs（避免和材质注入冲突）
        list += com.kael.texinject.paths.BuildPaths.iconDir(context)
        context.getExternalFilesDir(null)?.let {
            list += File(it, "TexInject/Resources")
            list += File(it, "TexInject")
        }
        list += File("/storage/emulated/0/Android/data/com.netease.x19/files/TexInject/Resources")
        list += File("/storage/emulated/0/Android/data/com.netease.x19/files/TexInject")
        return list
    }

    fun findIconFile(context: Context, sourceDir: File? = null): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in SPECIAL_NAMES) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    /**
     * 悬浮球 / 面板图标（支持 GIF 动图）。
     *
     * 优先用系统 ImageDecoder 解码：GIF 会返回会自动播放的 AnimatedImageDrawable
     * （API 28+，本模块目标设备均满足），PNG/JPG 则为普通 BitmapDrawable。
     * 解码失败回退到 Bitmap 版本 / 内置默认图标。
     */
    fun getLogoDrawable(context: Context, sourceDir: File? = null): android.graphics.drawable.Drawable {
        val file = findIconFile(context, sourceDir)
        if (file != null) {
            if (dropCached != null && drawableFrom == file.absolutePath) return dropCached!!
            try {
                if (android.os.Build.VERSION.SDK_INT >= 28) {
                    val src = android.graphics.ImageDecoder.createSource(file)
                    val d = android.graphics.ImageDecoder.decodeDrawable(src) { decoder, _, _ ->
                        decoder.allocator = android.graphics.ImageDecoder.ALLOCATOR_SOFTWARE
                    }
                    if (d is android.graphics.drawable.AnimatedImageDrawable) {
                        d.repeatCount = android.graphics.drawable.AnimatedImageDrawable.REPEAT_INFINITE
                        d.start()
                    }
                    dropCached = d
                    drawableFrom = file.absolutePath
                    return d
                }
            } catch (e: Throwable) {
            }
            // 老系统 / 解码失败：退回静态图
            try {
                BitmapFactory.decodeFile(file.absolutePath)?.let {
                    val d = android.graphics.drawable.BitmapDrawable(context.resources, it)
                    dropCached = d
                    drawableFrom = file.absolutePath
                    return d
                }
            } catch (e: Throwable) {
            }
        }
        val d = android.graphics.drawable.BitmapDrawable(context.resources, getLogo(context, sourceDir))
        dropCached = d
        drawableFrom = null
        return d
    }

    @Volatile private var dropCached: android.graphics.drawable.Drawable? = null
    @Volatile private var drawableFrom: String? = null

    fun getLogo(context: Context, sourceDir: File? = null): Bitmap {
        val file = findIconFile(context, sourceDir)
        if (file != null) {
            if (cached != null && cachedFrom == file.absolutePath) return cached!!
            try {
                BitmapFactory.decodeFile(file.absolutePath)?.let {
                    cached = it
                    cachedFrom = file.absolutePath
                    return it
                }
            } catch (e: Throwable) {}
        }
        cached?.let { if (!it.isRecycled && cachedFrom == null) return it }
        val fallback = makeDefaultIcon()
        cached = fallback
        cachedFrom = null
        return fallback
    }

    /** 内置默认图标：蓝底白字 "材"。 */
    private fun makeDefaultIcon(): Bitmap {
        val size = 144
        val bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        val bg = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = UiTheme.accentBlue }
        c.drawRoundRect(RectF(0f, 0f, size.toFloat(), size.toFloat()), 32f, 32f, bg)
        val tp = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = size * 0.5f
            textAlign = Paint.Align.CENTER
            isFakeBoldText = true
        }
        val fm = tp.fontMetrics
        val cy = size / 2f - (fm.ascent + fm.descent) / 2f
        c.drawText("材", size / 2f, cy, tp)
        return bmp
    }
}
