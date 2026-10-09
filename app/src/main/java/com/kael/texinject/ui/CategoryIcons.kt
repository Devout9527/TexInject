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
 * 分类图标加载器：从源目录（resource_packs）等处按名字取分类图标。
 *
 * 每个分类依次尝试这些文件名（任一即可）：
 *   • <分类名>.png        例：材质.png / 导入.png
 *   • icon<序号>.png      例：icon1.png … icon6.png（CreeperBox 风格）
 * 找不到则该分类只显示文字。
 */
object CategoryIcons {
    /**
     * 内置图标（矢量 drawable），按分类顺序对应：
     *   0 材质替换 / 1 音乐 / 2 脚本 / 3 快捷键 / 4 设置
     * 自定义 PNG 存在时优先用 PNG（内置 < 自定义）。
     */
    private val BUILTIN_ASSETS = arrayOf(
        "icons/cat_material.png",
        "icons/cat_music.png",
        "icons/cat_script.png",
        "icons/cat_hotkey.png",
        "icons/cat_settings.png"
    )

    /** 该分类的内置 PNG 位图（从模块 assets 加载，不走资源 ID）。 */
    fun builtinBitmap(context: Context, index: Int): android.graphics.Bitmap? {
        val path = BUILTIN_ASSETS.getOrNull(index) ?: return null
        return com.kael.texinject.ui.ModuleRes.loadAsset(context as android.app.Activity, path)
    }

    @Volatile
    private var cache = HashMap<String, Bitmap?>()

    fun clearCache() { cache = HashMap() }

    private fun dirs(context: Context, sourceDir: File?): List<File> {
        val list = mutableListOf<File>()
        list += com.kael.texinject.paths.BuildPaths.iconDir(context)
        sourceDir?.let { list += it }
        context.getExternalFilesDir(null)?.let { list += File(it, "TexInject") }
        list += File("/storage/emulated/0/Android/data/com.netease.x19/files/TexInject")
        return list
    }

    /** @param index 分类序号（0-based），用于 icon<N>.png 命名 */
    fun get(context: Context, sourceDir: File?, name: String, index: Int): Bitmap? {
        val key = "$name#$index"
        if (cache.containsKey(key)) return cache[key]
        val candidates = listOf("$name.png", "icon${index + 1}.png", "icon0${index + 1}.png")
        var result: Bitmap? = null
        outer@ for (dir in dirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (n in candidates) {
                val f = File(dir, n)
                if (f.isFile && f.length() > 0) {
                    result = try { BitmapFactory.decodeFile(f.absolutePath) } catch (e: Throwable) { null }
                    if (result != null) break@outer
                }
            }
        }
        cache[key] = result
        return result
    }
}
