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
 * 路径定义。
 *
 *  目标包目录（固定 FirstPatch 槽位）：
 *    <gameFiles>/games/com.netease/resource_packs/
 *      3.9_FirstPatch_2024_res_s1_texture_647d7cd2-1f2d-5959-a82f-c1093988afd0_0_0_2
 *  备份目录：
 *    <gameFiles>/TexInject/Backup/3.9_FirstPatch
 *
 *  <gameFiles> 在游戏进程内即 /data/user/0/当前游戏包名/files（= context.filesDir）。
 */
object GamePaths {

    /** 支持的游戏包名（正式版 + 开发者测试版）。 */
    val SUPPORTED_PACKAGES = setOf("com.netease.x19", "com.netease.mctest")

    /** 运行时检测到的游戏包名（init 时从 context.packageName 赋值）。 */
    @Volatile var pkg: String = "com.netease.x19"

    /** 运行时检测到的游戏版本号（如 3.9.15.297907 / 3.10.x），拿不到为空串。 */
    @Volatile var version: String = ""

    /**
     * 游戏版本是否 >= 3.10。
     *
     * 3.10 的 libminecraftpe 里 Python 符号全换了，FunctionsAddress::ADDR_310 还没补全
     * （setExecLocked 仍是 0）。在 3.10 上按 3.9 的偏移跳过去就是一个野地址 -> SIGSEGV。
     */
    val isAtLeast310: Boolean
        get() {
            val v = version
            if (v.isEmpty()) return false
            val parts = v.split('.')
            if (parts.size < 2) return false
            val major = parts[0].toIntOrNull() ?: return false
            val minor = parts[1].toIntOrNull() ?: return false
            return major > 3 || (major == 3 && minor >= 10)
        }

    /** 是否为开发者测试版（隐藏材质注入功能）。 */
    val isTestBuild: Boolean get() = pkg == "com.netease.mctest"

    const val MINECRAFT_ACTIVITY = "com.mojang.minecraftpe.MainActivity"

    /** 外部存储 files 目录（/storage/emulated/0/Android/data/<pkg>/files/<sub>）。 */
    fun extStorage(sub: String = ""): File =
        File("/storage/emulated/0/Android/data/$pkg/files/$sub")

    /** FirstPatch 槽位文件夹名。 */
    const val FIRSTPATCH_FOLDER =
        "3.9_FirstPatch_2024_res_s1_texture_647d7cd2-1f2d-5959-a82f-c1093988afd0_0_0_2"

    /** 目标包相对路径。 */
    const val TARGET_REL = "games/com.netease/resource_packs/$FIRSTPATCH_FOLDER"

    /** 备份相对路径。 */
    const val BACKUP_REL = "TexInject/Backup/3.9_FirstPatch"

    @Volatile
    private var gameFiles: File? = null

    /** 在游戏进程内用 context 初始化（filesDir 即 /data/user/0/当前游戏包名/files）。 */
    fun init(context: Context): File {
        pkg = context.packageName
        version = runCatching {
            context.packageManager.getPackageInfo(pkg, 0).versionName
        }.getOrNull().orEmpty()
        val f = context.filesDir
        gameFiles = f
        return f
    }

    /** 游戏私有 files 目录。 */
    val filesDir: File
        get() = gameFiles ?: File("/data/user/0/$pkg/files")

    /** 注入目标包目录（FirstPatch 槽位）。 */
    val targetPackDir: File
        get() = File(filesDir, TARGET_REL)

    /** 备份目录。 */
    val backupDir: File
        get() = File(filesDir, BACKUP_REL)

    /**
     * 源目录（用户放 .mcpack）。
     * 放在外部存储，用户无需 root 即可用文件管理器放入。
     */
    val sourceDir: File
        get() = extStorage("pack_netease/resource_packs").apply { mkdirs() }

    /** 确保目标与备份目录存在。 */
    fun ensureDirs(): Boolean {
        return try {
            targetPackDir.mkdirs()
            backupDir.mkdirs()
            true
        } catch (e: Throwable) {
            false
        }
    }

    val ready: Boolean
        get() = ensureDirs()
}
