/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui

import android.view.View
import java.lang.reflect.Field

/**
 * 图层级「跳过截屏 / 录屏」。
 *
 * AOSP 依据（frameworks/base/core/java/android/view/SurfaceControl.java）：
 *
 *     public static final int SKIP_SCREENSHOT = 0x00000040;   // 只影响截屏
 *     public static final int SECURE          = 0x00000080;   // 截屏 + 录屏都不出现
 *
 *     // @hide —— 不在公开 SDK 里，只能反射调用
 *     public Transaction setSkipScreenshot(SurfaceControl sc, boolean skipScreenshot)
 *     public Transaction setSecure(SurfaceControl sc, boolean isSecure)
 *
 * 注意：**没有** `setFlags(SurfaceControl,int,int)` 这个方法，
 * 早期版本按那个名字反射会直接 NoSuchMethodException 而静默失效。
 *
 * 前提：目标 View 必须在【独立窗口】里（PopupWindow / 自建窗口），
 * 这样它才有自己的 ViewRootImpl → SurfaceControl。
 * 挂在游戏 decorView 里的 View 与游戏共用图层，无法单独标记。
 */
object SkipScreenshot {

    private const val TAG = "[TexInject/SkipScreenshot]"

    /** 只跳过截屏（0x40）。 */
    const val MODE_SKIP_SCREENSHOT = 0

    /** 截屏 + 录屏都跳过（0x40 + 0x80）。 */
    const val MODE_BOTH = 1

    /**
     * 对某个 View 所在窗口的图层打标 / 取消打标。
     * @return 是否成功（失败不抛异常，仅记录日志）
     */
    fun apply(view: View, on: Boolean): Boolean {
        val sc = surfaceControlOf(view)
        if (sc == null) {
            log("SurfaceControl 获取失败（可能不在独立窗口，或隐藏 API 被拦）")
            return false
        }
        return try {
            val scClass = Class.forName("android.view.SurfaceControl")
            val txClass = Class.forName("android.view.SurfaceControl\$Transaction")
            val tx = txClass.getDeclaredConstructor().newInstance()

            // 只用 SKIP_SCREENSHOT(0x40)：
            //   该图层被"跳过"，截屏里看不到它，但下面的游戏图层照常入镜。
            // 绝不要用 SECURE(0x80)：那会让该区域在截屏里变【黑】，
            // 而我们的悬浮层是全屏的，一设就整屏全黑。
            val skip = txClass.getMethod("setSkipScreenshot", scClass, Boolean::class.javaPrimitiveType)
            skip.invoke(tx, sc, on)

            txClass.getMethod("apply").invoke(tx)
            log("apply($on) ok")
            true
        } catch (e: Throwable) {
            log("反射失败: ${e.javaClass.simpleName}: ${e.message}")
            false
        }
    }

    /** 取出 View 所在窗口的 SurfaceControl。 */
    private fun surfaceControlOf(view: View): Any? {
        // View -> AttachInfo
        val attachInfo = fieldByName(view, "mAttachInfo")
            ?: fieldByTypeName(view, "AttachInfo")
            ?: run { log("找不到 AttachInfo"); return null }

        // AttachInfo -> ViewRootImpl（字段名按类型查找，避免版本差异）
        val viewRootImpl = fieldByTypeName(attachInfo, "ViewRootImpl")
            ?: run { log("找不到 ViewRootImpl"); return null }

        // ViewRootImpl -> SurfaceControl
        return fieldByName(viewRootImpl, "mSurfaceControl")
            ?: fieldByTypeName(viewRootImpl, "SurfaceControl")
            ?: run { log("找不到 mSurfaceControl"); return null }
    }

    private fun fieldByName(target: Any, name: String): Any? {
        var c: Class<*>? = target.javaClass
        while (c != null) {
            try {
                val f = c.getDeclaredField(name)
                f.isAccessible = true
                return f.get(target)
            } catch (_: NoSuchFieldException) {
                c = c.superclass
            } catch (e: Throwable) {
                log("读字段 $name 异常: ${e.javaClass.simpleName}")
                return null
            }
        }
        return null
    }

    /** 按「字段类型的简单名包含 typeName」来找字段（跨版本更稳）。 */
    private fun fieldByTypeName(target: Any, typeName: String): Any? {
        var c: Class<*>? = target.javaClass
        while (c != null) {
            for (f: Field in c.declaredFields) {
                if (!f.type.simpleName.contains(typeName)) continue
                try {
                    f.isAccessible = true
                    return f.get(target)
                } catch (_: Throwable) {
                }
            }
            c = c.superclass
        }
        return null
    }

    private fun log(msg: String) {
        try {
            com.kael.texinject.TexInjectHookInit.logExternal("$TAG $msg")
        } catch (_: Throwable) {
        }
    }
}
