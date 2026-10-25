package com.kael.texinject.ui
import com.kael.texinject.GamePaths

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File

object PanelBackground {
    private val SPECIAL_NAMES = listOf(
        "background.png", "Background.png", "background.jpg", "Background.jpg",
        "bg.png", "Bg.png", "bg.jpg", "Bg.jpg",
        "panel.png", "menu.png", "cover.png", "Cover.png",
        "背景.png", "背景.jpg", "背景图.png", "面板.png"
    )

    @Volatile
    private var cached: Bitmap? = null
    @Volatile
    private var cachedFrom: String? = null

    fun clearCache() {
        cached = null
        cachedFrom = null
    }

    private fun candidateDirs(context: Context, sourceDir: File?): List<File> {
        val list = mutableListOf<File>()
        list += com.kael.texinject.paths.BuildPaths.iconDir(context)
        sourceDir?.let { list += it }
        context.getExternalFilesDir(null)?.let { list += File(it, "TexInject") }
        list += GamePaths.extStorage("TexInject")
        return list
    }

    fun findFile(context: Context, sourceDir: File?): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in SPECIAL_NAMES) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    private val VIDEO_NAMES = listOf(
        "background.mp4", "Background.mp4", "bg.mp4", "Bg.mp4",
        "panel.mp4", "menu.mp4", "背景.mp4", "背景视频.mp4", "面板.mp4",
        "background.webm", "bg.webm", "background.mkv"
    )

    fun findVideo(context: Context, sourceDir: File?): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in VIDEO_NAMES) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    fun get(context: Context, sourceDir: File?): Bitmap? {
        val file = findFile(context, sourceDir) ?: return null
        if (cached != null && cachedFrom == file.absolutePath) return cached
        return try {
            BitmapFactory.decodeFile(file.absolutePath)?.also {
                cached = it
                cachedFrom = file.absolutePath
            }
        } catch (e: Throwable) {
            null
        }
    }
}
