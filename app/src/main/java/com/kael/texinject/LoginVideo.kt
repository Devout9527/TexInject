package com.kael.texinject
import com.kael.texinject.GamePaths

import android.content.Context
import java.io.File

object LoginVideo {

    data class Result(val ok: Boolean, val summary: String)

    fun targetPath(context: Context): File =
        File(context.filesDir, "games/com.netease/storge/asset/loginVideoNew.mp4")

    private fun backupPath(context: Context): File =
        File(context.filesDir, "texinject/loginVideoNew.mp4.bak")

    private val SOURCE_NAMES = listOf(
        "loginVideoNew.mp4", "LoginVideo.mp4", "loginvideo.mp4",
        "登录视频.mp4", "主界面视频.mp4", "home.mp4", "login.mp4", "首页视频.mp4"
    )

    private fun candidateDirs(context: Context, sourceDir: File?): List<File> {
        val list = mutableListOf<File>()
        list += com.kael.texinject.paths.BuildPaths.iconDir(context)
        sourceDir?.let { list += it }
        context.getExternalFilesDir(null)?.let { list += File(it, "TexInject") }
        list += GamePaths.extStorage("TexInject")
        return list
    }

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
