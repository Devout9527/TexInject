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
 * 材质包注入入口：扫描源目录、调用 PackInjector（纯 Kotlin，不依赖内置 core.so）。
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
            val r = if (mode == 1) {
                PackInjector.inject(context, source, packs)
            } else {
                PackInjector.restore(context)
            }
            Result(r.ok, r.summary, r.lines)
        } catch (e: Throwable) {
            Result(false, "$verb 失败：${e.message}", listOf("ERR ${e.message}"))
        }
    }

    // 供 UI 状态显示用
    fun targetHasContent(): Boolean = PackInjector.targetHasContent()

    fun backupExists(): Boolean = PackInjector.backupExists()
}
