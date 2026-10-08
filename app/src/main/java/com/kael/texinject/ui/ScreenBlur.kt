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
package com.kael.texinject.ui

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Handler
import android.os.Looper
import android.view.PixelCopy

/**
 * 把游戏画面截图并做模糊，用于面板的毛玻璃底衬。
 *
 * 优先用 PixelCopy 抓取窗口（可含 GL 内容），失败则用 decorView.draw。
 * 模糊用"缩小再放大"的快速近似，配合半透明卡片得到毛玻璃观感。
 */
object ScreenBlur {

    private val main = Handler(Looper.getMainLooper())

    /** 抓取窗口画面（异步回调，主线程）。 */
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

    /** 抓取并模糊一步到位。 */
    fun captureBlurred(activity: Activity, radius: Int = 12, cb: (Bitmap?) -> Unit) {
        capture(activity) { src ->
            if (src == null) { cb(null); return@capture }
            cb(blur(src, radius))
        }
    }

    /** 快速模糊：缩小 → 放大（双线性插值产生模糊）。 */
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
