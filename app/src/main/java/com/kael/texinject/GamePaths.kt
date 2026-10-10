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
 *  <gameFiles> 在游戏进程内即 /data/user/0/com.netease.x19/files（= context.filesDir）。
 */
object GamePaths {

    const val MINECRAFT_PACKAGE = "com.netease.x19"
    const val MINECRAFT_ACTIVITY = "com.mojang.minecraftpe.MainActivity"

    /** FirstPatch 槽位文件夹名。 */
    const val FIRSTPATCH_FOLDER =
        "3.9_FirstPatch_2024_res_s1_texture_647d7cd2-1f2d-5959-a82f-c1093988afd0_0_0_2"

    /** 目标包相对路径。 */
    const val TARGET_REL = "games/com.netease/resource_packs/$FIRSTPATCH_FOLDER"

    /** 备份相对路径。 */
    const val BACKUP_REL = "TexInject/Backup/3.9_FirstPatch"

    @Volatile
    private var gameFiles: File? = null

    /** 在游戏进程内用 context 初始化（filesDir 即 /data/user/0/com.netease.x19/files）。 */
    fun init(context: Context): File {
        val f = context.filesDir
        gameFiles = f
        return f
    }

    /** 游戏私有 files 目录。 */
    val filesDir: File
        get() = gameFiles ?: File("/data/user/0/$MINECRAFT_PACKAGE/files")

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
        get() = File("/storage/emulated/0/Android/data/$MINECRAFT_PACKAGE/files/pack_netease/resource_packs").apply { mkdirs() }

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
