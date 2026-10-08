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

import android.content.Context
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.view.Surface
import android.view.TextureView
import android.widget.FrameLayout

/**
 * 居中裁剪的视频控件（TextureView + MediaPlayer + setTransform）。
 *
 * 视频按原始比例放大到"覆盖整个控件"，超出部分裁掉（center-crop），
 * 不会拉伸变形。用于右上角播放条等需要裁剪成特定形状的背景。
 */
class CenterCropVideo(context: Context) {

    private val texture = TextureView(context)
    private var player: MediaPlayer? = null
    private var path: String? = null
    private var surfaceReady = false
    private var surface: Surface? = null
    private var videoW = 0
    private var videoH = 0

    fun attach(container: FrameLayout) {
        if (texture.parent == null) {
            container.addView(
                texture,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT
                )
            )
            // 视图尺寸确定/变化时才算一次裁剪变换（不是每帧）
            texture.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> applyTransform() }
        }
        texture.visibility = android.view.View.VISIBLE
        texture.post { applyTransform() }
    }

    fun play(videoPath: String) {
        path = videoPath
        texture.visibility = android.view.View.VISIBLE
        if (surfaceReady && surface != null) start()
        else {
            texture.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
                override fun onSurfaceTextureAvailable(st: SurfaceTexture, w: Int, h: Int) {
                    surfaceReady = true
                    surface = Surface(st)
                    start()
                }
                override fun onSurfaceTextureSizeChanged(st: SurfaceTexture, w: Int, h: Int) {
                    applyTransform()
                }
                override fun onSurfaceTextureDestroyed(st: SurfaceTexture): Boolean {
                    surfaceReady = false
                    surface = null
                    release()
                    return true
                }
                override fun onSurfaceTextureUpdated(st: SurfaceTexture) {}
            }
        }
    }

    private fun start() {
        val p = path ?: return
        val s = surface ?: return
        release()
        try {
            val mp = MediaPlayer()
            mp.setSurface(s)
            mp.setAudioAttributes(
                AudioAttributes.Builder()
                    .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .build()
            )
            mp.setDataSource(p)
            mp.isLooping = true
            mp.setVolume(0f, 0f)
            mp.setOnVideoSizeChangedListener { _, vw, vh ->
                if (vw > 0 && vh > 0) { videoW = vw; videoH = vh; applyTransform() }
            }
            mp.setOnPreparedListener { it.start(); applyTransform() }
            mp.prepareAsync()
            player = mp
        } catch (e: Throwable) {
            release()
        }
    }

    /** 居中裁剪：把视频放大到"覆盖整个控件"（宽高都盖满），超出部分居中裁掉，不拉伸。 */
    private fun applyTransform() {
        val vw = texture.width
        val vh = texture.height
        if (vw <= 0 || vh <= 0 || videoW <= 0 || videoH <= 0) return
        // 覆盖整个视口的统一缩放（取大者，保证宽高都盖满）
        val cover = maxOf(vw.toFloat() / videoW, vh.toFloat() / videoH)
        val scaledW = videoW * cover
        val scaledH = videoH * cover
        val m = Matrix()
        // 默认把视频拉伸到 (vw, vh)；这里按正确比例放大到 (scaledW, scaledH) 再居中裁剪
        m.setScale(scaledW / vw, scaledH / vh, vw / 2f, vh / 2f)
        texture.setTransform(m)
    }

    private fun release() {
        try { player?.release() } catch (e: Throwable) {}
        player = null
    }

    fun hide() {
        texture.visibility = android.view.View.GONE
        release()
    }
}
