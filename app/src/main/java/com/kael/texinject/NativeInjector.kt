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
import java.io.InputStream

/**
 * 原生注入驱动 —— 在游戏进程内直接调用内置核心库（core.so）的 ks_entry。
 *
 * 重要：本模块运行在**游戏进程**里，context 是游戏的（com.netease.x19），
 * 因此不能用 context.assets（那是游戏的 assets），也不能用 PackageManager 查模块包
 * （Android 包可见性会拒绝）。正确做法是用**模块自己的 ClassLoader**读模块 APK 内的条目：
 *     assets/texinject/core.so            （注入核心原生库）
 *     lib/arm64-v8a/libtexbridge.so       （本模块的 JNI 桥）
 *
 * mode：1 = 注入，0 = 还原
 */
object NativeInjector {

    private const val ENTRY_CORE = "assets/texinject/core.so"
    private const val ENTRY_BRIDGE = "lib/arm64-v8a/libtexbridge.so"
    private const val DIR = "texinject"

    @Volatile
    private var nativeReady = false

    private external fun nativeInject(
        soPath: String, resDir: String, chan: String, filesRoot: String, mode: Int
    ): String

    /** core.so 已内置进 libtexbridge.so：这里只把内置字节释放到 soPath。 */
    private external fun nativeEnsureCore(soPath: String): Boolean

    @Synchronized
    private fun ensureNative(context: Context) {
        if (nativeReady) return
        try {
            System.loadLibrary("texbridge")
            nativeReady = true
            return
        } catch (e: Throwable) {
            // 回落：从模块 APK 提取 lib/arm64-v8a/libtexbridge.so 再 System.load
        }
        val lib = File(context.filesDir, "$DIR/libtexbridge.so")
        extractEntry(ENTRY_BRIDGE, lib)
        System.load(lib.absolutePath)
        nativeReady = true
    }

    /** 模块自身 ClassLoader 读取模块 APK 内条目。 */
    private fun openModuleEntry(entryName: String): InputStream {
        val cl = NativeInjector::class.java.classLoader
        cl?.getResourceAsStream(entryName)?.let { return it }
        // 兼容：有的宿主把条目当作 APK 根下的 zip 路径
        throw IllegalStateException("模块内未找到 $entryName")
    }

    private fun extractEntry(entryName: String, dst: File) {
        dst.parentFile?.mkdirs()
        openModuleEntry(entryName).use { input ->
            dst.outputStream().use { input.copyTo(it) }
        }
        if (!dst.exists() || dst.length() == 0L) {
            throw IllegalStateException("提取失败：$entryName")
        }
    }

    private fun ensureCore(context: Context): File {
        val dst = File(context.filesDir, "$DIR/core.so")
        dst.parentFile?.mkdirs()
        // 优先用内置字节（libtexbridge.so 里 .incbin 进来的 core.so）
        val ok = try { nativeEnsureCore(dst.absolutePath) } catch (e: Throwable) { false }
        if (!ok && (!dst.exists() || dst.length() == 0L)) {
            // 兜底：仍从模块 APK 的 assets 提取（旧路径）
            extractEntry(ENTRY_CORE, dst)
        }
        dst.setExecutable(true, false)
        return dst
    }

    /**
     * 执行注入/还原。
     * @param sourceDir 扫描 .mcpack 的源目录
     * @param mode 1=注入 0=还原
     * @return 结果行列表（来自 chan 文件）
     */
    fun run(context: Context, sourceDir: File, mode: Int): List<String> {
        ensureNative(context)
        val core = ensureCore(context)
        val chan = File(context.filesDir, "$DIR/chan.txt").apply {
            parentFile?.mkdirs()
            writeText("")
        }
        val filesRoot = context.filesDir.absolutePath
        nativeInject(core.absolutePath, sourceDir.absolutePath, chan.absolutePath, filesRoot, mode)
        return try {
            chan.readLines().filter { it.isNotBlank() }.also { chan.writeText("") }
        } catch (e: Throwable) {
            emptyList()
        }
    }
}
