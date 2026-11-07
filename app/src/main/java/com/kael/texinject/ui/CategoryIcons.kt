package com.kael.texinject.ui
import com.kael.texinject.GamePaths

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File

object CategoryIcons {

    private val BUILTIN_ASSETS = arrayOf(
        "icons/cat_material.png",
        "icons/cat_music.png",
        "icons/cat_script.png",
        "icons/cat_hotkey.png",
        "icons/cat_settings.png"
    )

    fun builtinBitmap(context: Context, index: Int): android.graphics.Bitmap? {
        val path = BUILTIN_ASSETS.getOrNull(index) ?: return null
        return com.kael.texinject.ui.ModuleRes.loadAsset(context as android.app.Activity, path)
    }

    @Volatile
    private var cache = HashMap<String, Bitmap?>()

    fun clearCache() { cache = HashMap() }

    private fun dirs(context: Context, sourceDir: File?): List<File> {
        val list = mutableListOf<File>()
        list += com.kael.texinject.paths.BuildPaths.iconDir(context)
        sourceDir?.let { list += it }
        context.getExternalFilesDir(null)?.let { list += File(it, "TexInject") }
        list += GamePaths.extStorage("TexInject")
        return list
    }

    fun get(context: Context, sourceDir: File?, name: String, index: Int): Bitmap? {
        val key = "$name#$index"
        if (cache.containsKey(key)) return cache[key]
        val candidates = listOf("$name.png", "icon${index + 1}.png", "icon0${index + 1}.png")
        var result: Bitmap? = null
        outer@ for (dir in dirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (n in candidates) {
                val f = File(dir, n)
                if (f.isFile && f.length() > 0) {
                    result = try { BitmapFactory.decodeFile(f.absolutePath) } catch (e: Throwable) { null }
                    if (result != null) break@outer
                }
            }
        }
        cache[key] = result
        return result
    }
}
