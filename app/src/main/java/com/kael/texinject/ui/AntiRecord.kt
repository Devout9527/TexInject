package com.kael.texinject.ui

import android.app.Activity
import android.content.Context
import android.view.View
import android.widget.PopupWindow
import java.lang.ref.WeakReference
import java.util.Collections

object AntiRecord {

    private const val PREF = "texinject_display"
    private const val KEY = "anti_record"

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun enabled(c: Context): Boolean = p(c).getBoolean(KEY, false)

    @Volatile
    private var lastActivity: Activity? = null

    private val livePopups = Collections.synchronizedList(mutableListOf<WeakReference<PopupWindow>>())

    fun remember(activity: Activity?) {
        lastActivity = activity
    }

    fun setEnabled(c: Context, on: Boolean) {
        p(c).edit().putBoolean(KEY, on).apply()
        OverlayManager.refreshAntiRecord()
        synchronized(livePopups) {
            livePopups.removeAll { it.get() == null }
            livePopups.forEach { ref -> ref.get()?.let { applyToPopup(it, on) } }
        }
    }

    fun refreshOverlay() {
        val ctx = lastActivity ?: return
        val ov = OverlayManager.overlayView() ?: return
        try {
            val ok = SkipScreenshot.apply(ov, enabled(ctx))
            com.kael.texinject.TexInjectHookInit.logExternal(
                "[TexInject/AntiRecord] overlay skip-screenshot=${enabled(ctx)} ok=$ok"
            )
        } catch (_: Throwable) {
        }
    }

    fun securePopup(pw: PopupWindow, context: Context) {
        synchronized(livePopups) {
            livePopups.removeAll { it.get() == null }
            if (livePopups.none { it.get() === pw }) {
                livePopups.add(WeakReference(pw))
            }
        }
        applyToPopup(pw, enabled(context))
    }

    fun applyToPopup(pw: PopupWindow, on: Boolean) {
        try {
            val cv: View? = pw.contentView
            if (cv != null) SkipScreenshot.apply(cv, on)
        } catch (_: Throwable) {
        }
    }
}
