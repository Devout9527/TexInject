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
import com.kael.texinject.nativecore.NativeCore
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 文件 I/O 追踪（开发者工具）。
 *
 * 底层是 native 的 PLT/GOT hook：把游戏对 openat / openat64 / __openat /
 * open / open64 / fopen / fopen64 / stat / lstat / access / faccessat /
 * opendir / mkdir / unlink / rename / rmdir / truncate 的调用接到
 * libtexinject.so 里，记录「调用者 RVA + 路径 + 返回值 + 打开方式」到一块
 * 16384 条的环形缓冲，再一次性落盘。
 *
 * 记录格式（每行一条，path 在最后且可能含逗号）：
 *     0x001A3F20,2,0,0,r,/data/user/0/com.netease.x19/files/games/.../manifest.json
 *     └ 调用者 RVA  └kind └ret └aux └mode └路径
 *
 * aux：open/openat 的 flags（O_CREAT/O_WRONLY 等），其余 kind 为 0。
 * mode：fopen 的 mode 字符串（r / rb / w / a+ …），其余 kind 为 "-"。
 * kind 的最高位 0x8000 表示路径超过 320 字节被截断，解析时记得 mask 掉。
 *
 * kind 对应 IOTRACE_KIND_*。
 *
 * 只在游戏进程内、且 native 库加载成功时可用；测试版（mctest）不注册 JNI，
 * 调用会抛 UnsatisfiedLinkError，这里统一兜住。
 */
object IoTrace {

    // 与 native 侧的 Kind 枚举保持一致
    const val KIND_OPENAT = 0
    const val KIND_OPEN = 1
    const val KIND_FOPEN = 2
    const val KIND_STAT = 3
    const val KIND_LSTAT = 4
    const val KIND_ACCESS = 5
    const val KIND_FACCESSAT = 6
    const val KIND_OPENDIR = 7
    const val KIND_MKDIR = 8
    const val KIND_UNLINK = 9
    const val KIND_RENAME = 10
    const val KIND_RMDIR = 11
    const val KIND_TRUNCATE = 12

    /** kind 里的截断标志位。 */
    const val TRUNC_FLAG = 0x8000

    private val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)

    /** 落盘目录：<files>/TexInject/trace/ */
    fun dir(context: Context): File =
        File(context.filesDir, "TexInject/trace").apply { runCatching { mkdirs() } }

    /** 开始记录。返回给用户看的一句话。 */
    fun start(): String = try {
        val imports = NativeCore.pltInit()
        if (imports <= 0) {
            "PLT 初始化失败：${NativeCore.pltLastError()}"
        } else if (!NativeCore.ioTraceStart()) {
            "启动失败：${NativeCore.ioTraceLastError()}"
        } else {
            "已开始记录（导入符号 $imports 个）"
        }
    } catch (t: Throwable) {
        "不可用：${t.message ?: t.javaClass.simpleName}"
    }

    fun stop(): String = try {
        NativeCore.ioTraceStop()
        "已停止（本轮共 ${count()} 条）"
    } catch (t: Throwable) {
        "不可用：${t.message ?: t.javaClass.simpleName}"
    }

    fun running(): Boolean = try {
        NativeCore.ioTraceRunning()
    } catch (t: Throwable) {
        false
    }

    fun count(): Int = try {
        NativeCore.ioTraceCount()
    } catch (t: Throwable) {
        0
    }

    /** 把缓冲写成文件；返回生成的文件，失败返回 null。 */
    fun dump(context: Context): File? = try {
        val f = File(dir(context), "io-${stamp.format(Date())}.txt")
        if (NativeCore.ioTraceDump(f.absolutePath)) f else null
    } catch (t: Throwable) {
        null
    }

    /**
     * 按关键字过滤一份 trace，方便只看材质相关的那几行。
     * 直接在内存里筛，不重写原文件。
     */
    fun filter(src: File, keyword: String): List<String> = runCatching {
        src.readLines().filter { it.contains(keyword, ignoreCase = true) }
    }.getOrDefault(emptyList())
}
