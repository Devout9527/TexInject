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

/**
 * 伪自定义目录：让游戏从我们自己的目录读某个资源包。
 *
 * 不新建包、不改 manifest、不碰官方包里的文件。只是把游戏本来就会加载的那个
 * 包目录（3.9 的 FirstPatch / 3.10 的 netease_client_packs/pack_<md5>）在
 * libminecraftpe.so 的 GOT 里换成我们的目录：
 *
 *   游戏读 <dataDir>/files/games/com.netease/resource_packs/3.9_FirstPatch_.../textures/x.png
 *   实际读 <外部 files>/pack_netease/active/textures/x.png
 *
 * 物理目录里没有的文件自动回退去读官方包（见 mount/ResourceSourceHook.cpp 的
 * fallback），所以只放被改过的文件就行；目录为空等于什么也没改。
 *
 * 写入、创建、删除、改名在这条别名路径上一律 EROFS —— 游戏碰不到我们的目录，
 * 官方包更新的解压动作也碰不到。
 *
 * 时机：Configure 可以在进程早期随便调；Install 必须等 libminecraftpe.so 装载后，
 * 由 native 的 waitForMinecraft 线程自动完成（见 native-lib.cpp）。所以开关是
 * 「写进 SP，下次启动生效」，不是即时热插。
 */
object ResourceMount {

    private const val SP = "texinject_mount"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_MODE = "mode"

    /** 注入方式：1 = 官方包，2 = 镜像目录。默认官方包。 */
    const val MODE_OFFICIAL = 1
    const val MODE_MIRROR = 2

    /**
     * 读注入方式。老版本存过 0（自动），现在没有这条路了，一律归到官方包 ——
     * 否则读到 0 会走进已经不存在的分支。
     */
    fun mode(context: Context): Int =
        if (context.getSharedPreferences(SP, Context.MODE_PRIVATE).getInt(KEY_MODE, MODE_OFFICIAL) == MODE_MIRROR)
            MODE_MIRROR else MODE_OFFICIAL

    fun setMode(context: Context, value: Int) {
        context.getSharedPreferences(SP, Context.MODE_PRIVATE).edit().putInt(KEY_MODE, value).apply()
    }

    /** 我们自己的物理目录：<外部 files>/pack_netease/active（用户可直接放文件）。 */
    fun storageDir(): File = GamePaths.extStorage("pack_netease/active").apply { mkdirs() }

    fun enabled(context: Context): Boolean =
        context.getSharedPreferences(SP, Context.MODE_PRIVATE).getBoolean(KEY_ENABLED, false)

    fun setEnabled(context: Context, on: Boolean) {
        context.getSharedPreferences(SP, Context.MODE_PRIVATE).edit()
            .putBoolean(KEY_ENABLED, on).apply()
    }

    /**
     * 别名后缀：目标包目录相对 dataDir 的部分，例如
     * `/files/games/com.netease/resource_packs/3.9_FirstPatch_...`。
     * 找不到目标包时返回 null（此时不装，避免把整个 dataDir 重定向掉）。
     */
    fun aliasSuffix(context: Context): String? {
        val target = findTarget() ?: return null
        return suffixOf(context, target.dir)
    }

    /**
     * 目标包目录相对 dataDir 的后缀。
     *
     * 坑：`/data/user/0/...` 和 `/data/data/...` 是同一目录的两种写法，canonicalPath
     * 会把其中一个换成另一个。所以两边都得按 canonical 比，再拿原始写法兜一次，
     * 否则永远 startsWith 不上（症状就是「没找到目标资源包」）。
     */
    private fun suffixOf(context: Context, dir: File): String? {
        val raw = runCatching { context.applicationInfo.dataDir }.getOrNull() ?: return null
        val canonical = runCatching { File(raw).canonicalPath }.getOrDefault(raw)
        val path = runCatching { dir.canonicalPath }.getOrElse { dir.absolutePath }
        for (base in arrayOf(canonical, raw)) {
            if (path.startsWith("$base/")) return path.removePrefix(base)
        }
        return null
    }

    /** 找目标包：先问 PackInjector，它挑不出来（score 全是 0）再按名字兜底。 */
    private fun findTarget(): PackInjector.Target? {
        runCatching { PackInjector.findTarget() }.getOrNull()?.let { return it }
        val root = File(GamePaths.filesDir, "games/com.netease/resource_packs")
        val candidates = root.listFiles { f -> f.isDirectory && f.name.contains("FirstPatch", true) }
            ?: return null
        val best = candidates.filter { File(it, "textures").isDirectory }
            .maxByOrNull { it.lastModified() }
            ?: candidates.maxByOrNull { it.lastModified() }
            ?: return null
        return PackInjector.Target(best, "3.9")
    }

    /** 只配置，不安装。进程早期调用。 */
    fun configure(context: Context): Boolean = try {
        val dataDir = context.applicationInfo.dataDir
        val alias = aliasSuffix(context) ?: return false
        val canonical = runCatching { File(dataDir).canonicalPath }.getOrDefault(dataDir)
        NativeCore.mountConfigure(dataDir, canonical, alias, storageDir().canonicalPath)
    } catch (t: Throwable) {
        false
    }

    /**
     * 注入前调用：确保映射已配置并装上，装上才算方式二可用。
     *
     * 只有真的 mounted 才写开关，否则下次启动会配置一个空的映射目录，
     * 虽然 fallback 能救回官方包，但没必要多这一层。
     */
    fun ensure(context: Context): Boolean {
        if (!configure(context)) {
            setEnabled(context, false)
            return false
        }
        val mounted = try {
            NativeCore.mountInstall() && NativeCore.mountStatus() == "mounted"
        } catch (t: Throwable) {
            false
        }
        setEnabled(context, mounted)
        return mounted
    }

    /** 方式二还原：清空镜像目录，关掉开关。返回删掉的文件数。 */
    fun clear(context: Context): Int {
        var removed = 0
        val root = storageDir()
        root.walkBottomUp().forEach { f ->
            if (f != root && f.isFile && f.delete()) removed++
        }
        setEnabled(context, false)
        return removed
    }

    fun status(): String = try {
        when (val s = NativeCore.mountStatus()) {
            "mounted" -> "已生效"
            "waiting_for_file_imports" -> "等待游戏加载"
            "not_configured" -> "未配置"
            else -> s
        }
    } catch (t: Throwable) {
        "不可用：${t.message ?: t.javaClass.simpleName}"
    }
}
