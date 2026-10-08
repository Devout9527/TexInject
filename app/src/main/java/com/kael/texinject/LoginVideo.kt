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
package com.kael.texinject

import android.content.Context
import java.io.File

/**
 * 游戏主界面（登录）视频替换 —— 对齐 KuSug 的 loginVideo 模块。
 *
 * 【声明】本功能（材质包注入 / 主界面视频替换）移植自 KuSug UI，原作者：bi匕匕bi。
 *  致谢 KuSug 项目提供的网易版材质/视频替换思路与路径。
 *
 * 机制：把用户提供的 .mp4 覆盖到游戏官方视频位置，首次替换前备份官方原片，
 * 恢复时用备份覆盖回去。重启游戏生效。
 *
 *   目标：<filesDir>/games/com.netease/storge/asset/loginVideoNew.mp4
 *   （NetEase 官方路径把 storage 拼成了 storge，保持原样）
 *
 *   源：在源目录（resource_packs）等处按特殊命名查找 .mp4。
 *   备份：<filesDir>/texinject/loginVideoNew.mp4.bak
 */
object LoginVideo {

    data class Result(val ok: Boolean, val summary: String)

    /** 官方目标路径（注意是 storge）。 */
    fun targetPath(context: Context): File =
        File(context.filesDir, "games/com.netease/storge/asset/loginVideoNew.mp4")

    /** 备份路径。 */
    private fun backupPath(context: Context): File =
        File(context.filesDir, "texinject/loginVideoNew.mp4.bak")

    /** 支持的特殊文件名（任一即可）。 */
    private val SOURCE_NAMES = listOf(
        "loginVideoNew.mp4", "LoginVideo.mp4", "loginvideo.mp4",
        "登录视频.mp4", "主界面视频.mp4", "home.mp4", "login.mp4", "首页视频.mp4"
    )

    private fun candidateDirs(context: Context, sourceDir: File?): List<File> {
        val list = mutableListOf<File>()
        list += com.kael.texinject.paths.BuildPaths.iconDir(context)
        sourceDir?.let { list += it }
        context.getExternalFilesDir(null)?.let { list += File(it, "TexInject") }
        list += File("/storage/emulated/0/Android/data/com.netease.x19/files/TexInject")
        return list
    }

    /** 查找用户提供的源视频文件。 */
    fun findSource(context: Context, sourceDir: File?): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in SOURCE_NAMES) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    /** 替换主界面视频。 */
    fun replace(context: Context, sourceDir: File): Result {
        GamePaths.init(context)
        val target = targetPath(context)
        if (!target.exists()) {
            return Result(false, "目标不存在：${target.name}")
        }
        val src = findSource(context, sourceDir)
            ?: return Result(false, "未找到源视频（放入 ${SOURCE_NAMES.first()} 后重试）")
        return try {
            val bak = backupPath(context)
            bak.parentFile?.mkdirs()
            // 首次替换前备份官方原片（避免重复备份覆盖）
            if (!bak.exists() || bak.length() == 0L) {
                target.inputStream().use { input ->
                    bak.outputStream().use { input.copyTo(it, 8192) }
                }
            }
            src.inputStream().use { input ->
                target.outputStream().use { input.copyTo(it, 8192) }
            }
            Result(true, "已替换主界面视频，请重启游戏生效")
        } catch (e: Throwable) {
            Result(false, "替换失败：${e.message}")
        }
    }

    /** 恢复官方视频。 */
    fun restore(context: Context): Result {
        GamePaths.init(context)
        val target = targetPath(context)
        val bak = backupPath(context)
        if (!bak.exists() || bak.length() == 0L) {
            return Result(false, "无官方备份可恢复（先执行一次替换）")
        }
        return try {
            bak.inputStream().use { input ->
                target.outputStream().use { input.copyTo(it, 8192) }
            }
            Result(true, "已恢复官方视频，请重启游戏生效")
        } catch (e: Throwable) {
            Result(false, "恢复失败：${e.message}")
        }
    }
}
