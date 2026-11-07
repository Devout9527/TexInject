package com.kael.texinject.ui

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Handler
import android.os.Looper
import android.view.PixelCopy

object ScreenBlur {

    private val main = Handler(Looper.getMainLooper())

    fun capture(activity: Activity, cb: (Bitmap?) -> Unit) {
        if (activity.isFinishing || activity.isDestroyed) { cb(null); return }
        val decor = activity.window.decorView
        val w = decor.width
        val h = decor.height
        if (w <= 0 || h <= 0) { cb(null); return }
        val bmp = try {
            Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        } catch (e: Throwable) { cb(null); return }

        try {
            PixelCopy.request(activity.window, bmp, { result ->
                if (result == PixelCopy.SUCCESS) cb(bmp) else cb(null)
            }, main)
        } catch (e: Throwable) {
            try {
                val c = Canvas(bmp)
                decor.draw(c)
                cb(bmp)
            } catch (e2: Throwable) {
                cb(null)
            }
        }
    }

    fun captureBlurred(activity: Activity, radius: Int = 12, cb: (Bitmap?) -> Unit) {
        capture(activity) { src ->
            if (src == null) { cb(null); return@capture }
            cb(blur(src, radius))
        }
    }

    fun blur(src: Bitmap, radius: Int): Bitmap {
        val r = radius.coerceAtLeast(1)
        val sw = src.width
        val sh = src.height
        val dw = (sw / r).coerceAtLeast(1)
        val dh = (sh / r).coerceAtLeast(1)
        val small = Bitmap.createScaledBitmap(src, dw, dh, true)
        val out = Bitmap.createScaledBitmap(small, sw, sh, true)
        if (small != out && !small.isRecycled) small.recycle()
        return out
    }
}
