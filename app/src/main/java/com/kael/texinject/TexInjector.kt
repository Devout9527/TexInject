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
import com.kael.texinject.ui.compose.DevMode
import java.io.File

/**
 * 材质包注入入口：扫描源目录、调用 PackInjector。
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
            if (mode == 1) {
                // 方式由面板上的二选决定：官方包 / 镜像目录。
                // 二选只在开发者模式下可见；开发模式关着时一律走官方包，免得以前
                // 选过镜像的机器继续读 pack_netease/active。
                DevMode.ensure(context)
                val choice = if (DevMode.enabled) ResourceMount.mode(context)
                             else ResourceMount.MODE_OFFICIAL
                val mirrored = if (choice == ResourceMount.MODE_MIRROR) {
                    if (!ResourceMount.ensure(context)) {
                        return Result(
                            false,
                            "镜像目录装不上（找不到目标包或导入不全），已取消注入；" +
                                "改成「官方包」再试",
                            listOf("提示：镜像需要先找到游戏正在加载的资源包目录"),
                        )
                    }
                    true
                } else false
                val r = if (mirrored && packs.isNotEmpty()) {
                    PackInjector.injectToMirror(source, packs, ResourceMount.storageDir())
                } else {
                    // 走官方包就必须把映射关掉，否则重启后游戏还在读镜像目录
                    if (choice == ResourceMount.MODE_OFFICIAL) ResourceMount.setEnabled(context, false)
                    PackInjector.inject(context, source, packs)
                }
                Result(r.ok, r.summary, r.lines)
            } else {
                // 两条路的残留都清掉：镜像目录 + 官方的注入备份
                val removed = ResourceMount.clear(context)
                val r = PackInjector.restore(context)
                val summary = if (removed > 0) {
                    "已清空映射目录（$removed 个文件）／${r.summary}"
                } else r.summary
                Result(r.ok || removed > 0, summary, r.lines)
            }
        } catch (e: Throwable) {
            Result(false, "$verb 失败：${e.message}", listOf("ERR ${e.message}"))
        }
    }

    // 供 UI 状态显示用
    fun targetHasContent(): Boolean = PackInjector.targetHasContent()

    fun backupExists(): Boolean = PackInjector.backupExists()
}
