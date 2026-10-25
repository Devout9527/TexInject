package com.kael.texinject.ui

import android.app.Activity
import android.content.Context
import android.content.res.Resources
import android.graphics.drawable.Drawable

object ModuleRes {
    private const val MODULE_PKG = "com.kael.texinject"

    @Volatile private var cached: Resources? = null

    fun of(activity: Activity): Resources {
        cached?.let { return it }
        try {
            val ai = activity.packageManager.getApplicationInfo(MODULE_PKG, 0)
            val am = android.content.res.AssetManager::class.java.newInstance()
            val add = android.content.res.AssetManager::class.java
                .getDeclaredMethod("addAssetPath", String::class.java)
            add.isAccessible = true
            add.invoke(am, ai.sourceDir)
            val r = Resources(am, activity.resources.displayMetrics, activity.resources.configuration)
            cached = r
            return r
        } catch (e: Throwable) {
        }
        try {
            val r = activity.createPackageContext(MODULE_PKG, Context.CONTEXT_IGNORE_SECURITY).resources
            cached = r
            return r
        } catch (e: Throwable) {
        }
        val r = activity.resources
        cached = r
        return r
    }

    fun drawable(activity: Activity, id: Int): Drawable? = try {
        of(activity).getDrawable(id)
    } catch (e: Throwable) {
        null
    }
    fun loadAsset(activity: Activity, path: String): android.graphics.Bitmap? {
        return try {
            val am = of(activity).assets
            am.open(path).use { stream ->
                android.graphics.BitmapFactory.decodeStream(stream)
            }
        } catch (e: Throwable) {
            android.util.Log.e("TexInjectRes", "loadAsset $path failed", e)
            null
        }
    }

}
