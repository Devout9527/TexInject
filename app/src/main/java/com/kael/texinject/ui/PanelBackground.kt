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
import java.io.File

/**
 * 面板背景图加载器。
 *
 * 在若干目录里找**特殊命名**的图片作为面板背景，找到即用：
 *   特殊文件名：background.png / bg.png / panel.png / menu.png / 背景.png / 面板.png
 *   搜索目录（按优先级）：
 *     1. 源目录（resource_packs）—— 用户最方便放入
 *     2. <模块外部目录>/TexInject/
 *     3. <游戏外部目录>/TexInject/
 * 找不到则不使用背景（沿用纯色卡片）。
 */
object PanelBackground {
    private val SPECIAL_NAMES = listOf(
        "background.png", "Background.png", "background.jpg", "Background.jpg",
        "bg.png", "Bg.png", "bg.jpg", "Bg.jpg",
        "panel.png", "menu.png", "cover.png", "Cover.png",
        "背景.png", "背景.jpg", "背景图.png", "面板.png"
    )

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
        list += com.kael.texinject.paths.BuildPaths.iconDir(context)
        sourceDir?.let { list += it }
        context.getExternalFilesDir(null)?.let { list += File(it, "TexInject") }
        list += File("/storage/emulated/0/Android/data/com.netease.x19/files/TexInject")
        return list
    }

    fun findFile(context: Context, sourceDir: File?): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in SPECIAL_NAMES) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    private val VIDEO_NAMES = listOf(
        "background.mp4", "Background.mp4", "bg.mp4", "Bg.mp4",
        "panel.mp4", "menu.mp4", "背景.mp4", "背景视频.mp4", "面板.mp4",
        "background.webm", "bg.webm", "background.mkv"
    )

    /** 查找可作为面板背景的视频（优先于背景图）。 */
    fun findVideo(context: Context, sourceDir: File?): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in VIDEO_NAMES) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    fun get(context: Context, sourceDir: File?): Bitmap? {
        val file = findFile(context, sourceDir) ?: return null
        if (cached != null && cachedFrom == file.absolutePath) return cached
        return try {
            BitmapFactory.decodeFile(file.absolutePath)?.also {
                cached = it
                cachedFrom = file.absolutePath
            }
        } catch (e: Throwable) {
            null
        }
    }
}
