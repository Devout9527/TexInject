package com.kael.texinject.ui

import android.content.Context
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.view.Surface
import android.view.TextureView
import android.widget.FrameLayout

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

    private fun applyTransform() {
        val vw = texture.width
        val vh = texture.height
        if (vw <= 0 || vh <= 0 || videoW <= 0 || videoH <= 0) return
        val cover = maxOf(vw.toFloat() / videoW, vh.toFloat() / videoH)
        val scaledW = videoW * cover
        val scaledH = videoH * cover
        val m = Matrix()
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
