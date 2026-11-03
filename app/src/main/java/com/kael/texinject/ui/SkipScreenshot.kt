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
     *
     * 如果 view 还没 attach 到窗口（mAttachInfo 为 null），会等到它 attach 之后再打，
     * 否则调用瞬间必然失败 —— 「挂载步骤 7 打标 / 13ms 后才 attached」就是这个坑。
     *
     * @return 场景：立即成功返回 true；需要等 attach 时返回 true（已排队）；
     *         连 SurfaceControl 都取不到返回 false。
     */
    fun apply(view: View, on: Boolean): Boolean {
        // 没 attach 就先排队，等真正上屏再打标
        if (!view.isAttachedToWindow) {
            log("view 尚未 attach，排队等 attach 后打标 (on=$on)")
            view.addOnAttachStateChangeListener(object : android.view.View.OnAttachStateChangeListener {
                override fun onViewAttachedToWindow(v: View) {
                    v.removeOnAttachStateChangeListener(this)
                    // attach 后图层仍可能晚一帧才建好，post 一次再打
                    v.post { applyNow(v, on) }
                }
                override fun onViewDetachedFromWindow(v: View) = Unit
            })
            return true
        }
        return applyNow(view, on)
    }

    /** 真正的打标动作（view 已 attach）。 */
    private fun applyNow(view: View, on: Boolean): Boolean {
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
            //
            // 方法名跨版本有差异，两个都试：
            //   setSkipScreenshot(SurfaceControl, boolean)  —— Android 12L+
            //   setFlags(SurfaceControl, int, int)          —— 老的通用写法
            val flag = 0x40
            var done = false
            try {
                val m = txClass.getMethod(
                    "setSkipScreenshot", scClass, Boolean::class.javaPrimitiveType
                )
                m.invoke(tx, sc, on)
                done = true
            } catch (e: NoSuchMethodException) {
                log("setSkipScreenshot 不存在，改用 setFlags")
            }
            if (!done) {
                val m = txClass.getMethod(
                    "setFlags", scClass, Int::class.javaPrimitiveType, Int::class.javaPrimitiveType
                )
                m.invoke(tx, sc, if (on) flag else 0, flag)
            }

            txClass.getMethod("apply").invoke(tx)
            log("apply($on) ok (via ${if (done) "setSkipScreenshot" else "setFlags"})")
            true
        } catch (e: Throwable) {
            // 把真实原因分清楚：方法不存在 / 被隐藏 API 拦 / 其它
            val cause = e.cause ?: e
            log("反射失败: ${cause.javaClass.simpleName}: ${cause.message}")
            false
        }
    }

    /** 取出 View 所在窗口的 SurfaceControl。 */
    private fun surfaceControlOf(view: View): Any? {
        // 路径 A：Android 12+ 的公开接口 View.getRootSurfaceControl()，
        //         它的实现就是 ViewRootImpl 自己，再反射拿 mSurfaceControl。
        //         比走 mAttachInfo 少一层，也更不容易踩到隐藏 API 限制。
        run {
            try {
                val root = view.rootSurfaceControl   // AttachedSurfaceControl?
                if (root != null) {
                    val sc = fieldByName(root, "mSurfaceControl")
                        ?: fieldByTypeName(root, "SurfaceControl")
                    if (sc != null) return sc
                }
            } catch (e: Throwable) {
                log("路径A(getRootSurfaceControl) 失败: ${e.javaClass.simpleName}")
            }
        }

        // 路径 B：老的 mAttachInfo -> ViewRootImpl -> mSurfaceControl
        val attachInfo = fieldByName(view, "mAttachInfo")
            ?: run { log("找不到 AttachInfo（view 可能未 attach）"); return null }

        val viewRootImpl = fieldByTypeName(attachInfo, "ViewRootImpl")
            ?: run { log("找不到 ViewRootImpl"); return null }

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
