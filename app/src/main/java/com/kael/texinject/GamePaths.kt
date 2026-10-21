package com.kael.texinject

import android.content.Context
import java.io.File

object GamePaths {

    val SUPPORTED_PACKAGES = setOf("com.netease.x19", "com.netease.mctest")

    @Volatile var pkg: String = "com.netease.x19"

    @Volatile var version: String = ""

    val isTestBuild: Boolean get() = pkg == "com.netease.mctest"

    const val MINECRAFT_ACTIVITY = "com.mojang.minecraftpe.MainActivity"

    fun extStorage(sub: String = ""): File =
        File("/storage/emulated/0/Android/data/$pkg/files/$sub")

    const val FIRSTPATCH_FOLDER =
        "3.9_FirstPatch_2024_res_s1_texture_647d7cd2-1f2d-5959-a82f-c1093988afd0_0_0_2"

    const val TARGET_REL = "games/com.netease/resource_packs/$FIRSTPATCH_FOLDER"

    const val BACKUP_REL = "TexInject/Backup/3.9_FirstPatch"

    @Volatile
    private var gameFiles: File? = null

    fun init(context: Context): File {
        pkg = context.packageName
        version = runCatching {
            context.packageManager.getPackageInfo(pkg, 0).versionName
        }.getOrNull().orEmpty()
        val f = context.filesDir
        gameFiles = f
        return f
    }

    val filesDir: File
        get() = gameFiles ?: File("/data/user/0/$pkg/files")

    val targetPackDir: File
        get() = File(filesDir, TARGET_REL)

    val backupDir: File
        get() = File(filesDir, BACKUP_REL)

    val sourceDir: File
        get() = extStorage("pack_netease/resource_packs").apply { mkdirs() }

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
