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
import android.content.Context
import android.content.res.Resources
import android.graphics.drawable.Drawable

/**
 * 取**模块自己的**资源。
 *
 * 关键：游戏内我们的 View 挂在 com.netease.x19 的 Activity 上，
 * `view.resources` / `setImageResource(R.drawable.x)` 用的都是**宿主游戏**的资源表，
 * 而 R.drawable.x 是**模块**的 ID —— 两边对不上，图标就画不出来
 * （桌面入口时 Activity 是模块自己，所以看起来正常）。
 *
 * 所以凡是引用模块自带 drawable 的地方，都要走这里拿 Resources。
 */
object ModuleRes {
    private const val MODULE_PKG = "com.kael.texinject"

    @Volatile private var cached: Resources? = null

    fun of(activity: Activity): Resources {
        cached?.let { return it }
        // 方案 1：AssetManager 直接挂载模块 APK 的资源（最可靠）
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
        // 方案 2：createPackageContext
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

    /** 取模块 drawable；取不到返回 null（调用方自行兜底）。 */
    fun drawable(activity: Activity, id: Int): Drawable? = try {
        of(activity).getDrawable(id)
    } catch (e: Throwable) {
        null
    }
}

    /** 从模块 APK 的 assets 里加载一个 PNG 位图（不走资源 ID，彻底绕开跨进程资源表问题）。 */
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
