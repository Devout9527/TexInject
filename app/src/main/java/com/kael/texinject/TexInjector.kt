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
 * 材质包注入入口：驱动内置核心库完成注入（见 NativeInjector）。
 *
 * 【声明】本功能（材质包注入 / 主界面视频替换）移植自 KuSug UI，原作者：bi匕匕bi。
 *  致谢 KuSug 项目提供的网易版材质注入思路与路径。
 *
 * 核心库内部完成：FirstPatch 槽位落位、.bak 备份、解压覆盖、
 * folder_md5 重算、meta.json/manifest.json 的 \"rel\"/\"had\" 索引修补。
 * 本类只负责扫描源目录、调用、解析结果。
 */
object TexInjector {

    data class Result(val ok: Boolean, val summary: String, val detail: List<String>)

    /** 扫描源目录下的 .mcpack / .zip。 */
    fun scanPacks(source: File): List<File> {
        if (!source.isDirectory) return emptyList()
        return source.listFiles { f ->
            f.isFile && (f.name.endsWith(".mcpack", true) || f.name.endsWith(".zip", true))
        }?.sortedBy { it.name } ?: emptyList()
    }

    /** 注入源目录下全部材质包（mode=1）。 */
    fun injectAll(context: Context, source: File): Result {
        val packs = scanPacks(source)
        if (packs.isEmpty()) {
            return Result(false, "源目录无材质包：${source.absolutePath}", emptyList())
        }
        return run(context, source, packs, mode = 1, verb = "注入")
    }

    /** 注入单个材质包（核心库按目录扫描，先确认文件存在）。 */
    fun inject(context: Context, source: File, file: File): Result {
        if (!file.isFile) return Result(false, "文件不存在：${file.name}", emptyList())
        return run(context, source, listOf(file), mode = 1, verb = "注入")
    }

    /** 还原官方材质（mode=0）。 */
    fun restore(context: Context, source: File): Result =
        run(context, source, scanPacks(source), mode = 0, verb = "还原")

    private fun run(
        context: Context, source: File, packs: List<File>, mode: Int, verb: String
    ): Result {
        return try {
            GamePaths.init(context)
            val lines = NativeInjector.run(context, source, mode)
            val joined = lines.joinToString("\n")
            val failed = lines.any { it.startsWith("ERR") || it.contains("失败") || it.contains("FAIL") }
            val okLine = lines.firstOrNull { it.startsWith("OK") || it.contains("成功") }
            if (failed && okLine == null) {
                Result(false, "$verb 失败：${lines.firstOrNull() ?: "无返回"}", lines)
            } else {
                val n = packs.size
                val summary = if (mode == 1) "$verb 完成（$n 个），请重启游戏" else "已还原官方材质，请重启游戏"
                Result(true, summary, lines.ifEmpty { listOf("OK (chan empty)") })
            }
        } catch (e: Throwable) {
            Result(false, "$verb 失败：${e.message}", listOf("ERR ${e.message}"))
        }
    }

    // 供 UI 状态显示用：目标槽位是否已有内容
    fun targetHasContent(): Boolean {
        val d = GamePaths.targetPackDir
        return d.isDirectory && d.listFiles()?.isNotEmpty() == true
    }

    fun backupExists(): Boolean {
        val d = GamePaths.backupDir
        return d.isDirectory && d.listFiles()?.isNotEmpty() == true
    }
}
