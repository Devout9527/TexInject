package com.kael.texinject.ui
import com.kael.texinject.GamePaths

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import java.io.File

object IconLoader {
    private val SPECIAL_NAMES = listOf(
        "Icon.png", "icon.png", "Logo.png", "logo.png", "Cover.png", "cover.png"
    )

    private val VIDEO_NAMES = listOf(
        "Ball.mp4", "ball.mp4", "球.mp4", "球视频.mp4",
        "Logo.mp4", "logo.mp4", "Icon.mp4", "icon.mp4",
        "Cover.mp4", "cover.mp4", "图标.mp4", "面板.mp4"
    )

    private fun findByName(context: Context, sourceDir: File?, names: List<String>): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in names) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    fun findBallVideo(context: Context, sourceDir: File? = null): File? =
        findByName(context, sourceDir, VIDEO_NAMES)

    fun findLogoVideo(context: Context, sourceDir: File? = null): File? =
        findByName(context, sourceDir, VIDEO_NAMES)

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
        context.getExternalFilesDir(null)?.let {
            list += File(it, "TexInject/Resources")
            list += File(it, "TexInject")
        }
        list += GamePaths.extStorage("TexInject/Resources")
        list += GamePaths.extStorage("TexInject")
        return list
    }

    fun findIconFile(context: Context, sourceDir: File? = null): File? {
        for (dir in candidateDirs(context, sourceDir)) {
            if (!dir.isDirectory) continue
            for (name in SPECIAL_NAMES) {
                val f = File(dir, name)
                if (f.isFile && f.length() > 0) return f
            }
        }
        return null
    }

    fun getLogoDrawable(context: Context, sourceDir: File? = null): android.graphics.drawable.Drawable {
        val file = findIconFile(context, sourceDir)
        if (file != null) {
            if (dropCached != null && drawableFrom == file.absolutePath) return dropCached!!
            try {
                if (android.os.Build.VERSION.SDK_INT >= 28) {
                    val src = android.graphics.ImageDecoder.createSource(file)
                    val d = android.graphics.ImageDecoder.decodeDrawable(src) { decoder, _, _ ->
                        decoder.allocator = android.graphics.ImageDecoder.ALLOCATOR_SOFTWARE
                    }
                    if (d is android.graphics.drawable.AnimatedImageDrawable) {
                        d.repeatCount = android.graphics.drawable.AnimatedImageDrawable.REPEAT_INFINITE
                        d.start()
                    }
                    dropCached = d
                    drawableFrom = file.absolutePath
                    return d
                }
            } catch (e: Throwable) {
            }
            try {
                BitmapFactory.decodeFile(file.absolutePath)?.let {
                    val d = android.graphics.drawable.BitmapDrawable(context.resources, it)
                    dropCached = d
                    drawableFrom = file.absolutePath
                    return d
                }
            } catch (e: Throwable) {
            }
        }
        val d = android.graphics.drawable.BitmapDrawable(context.resources, getLogo(context, sourceDir))
        dropCached = d
        drawableFrom = null
        return d
    }

    @Volatile private var dropCached: android.graphics.drawable.Drawable? = null
    @Volatile private var drawableFrom: String? = null

    fun getLogo(context: Context, sourceDir: File? = null): Bitmap {
        val file = findIconFile(context, sourceDir)
        if (file != null) {
            if (cached != null && cachedFrom == file.absolutePath) return cached!!
            try {
                BitmapFactory.decodeFile(file.absolutePath)?.let {
                    cached = it
                    cachedFrom = file.absolutePath
                    return it
                }
            } catch (e: Throwable) {}
        }
        cached?.let { if (!it.isRecycled && cachedFrom == null) return it }
        val fallback = makeDefaultIcon()
        cached = fallback
        cachedFrom = null
        return fallback
    }

    private fun makeDefaultIcon(): Bitmap {
        val size = 144
        val bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        val bg = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = UiTheme.accentBlue }
        c.drawRoundRect(RectF(0f, 0f, size.toFloat(), size.toFloat()), 32f, 32f, bg)
        val tp = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textSize = size * 0.5f
            textAlign = Paint.Align.CENTER
            isFakeBoldText = true
        }
        val fm = tp.fontMetrics
        val cy = size / 2f - (fm.ascent + fm.descent) / 2f
        c.drawText("材", size / 2f, cy, tp)
        return bmp
    }
}
