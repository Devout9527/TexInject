package com.kael.texinject.ui

import android.view.View
import java.lang.reflect.Field

object SkipScreenshot {

    private const val TAG = "[TexInject/SkipScreenshot]"

    const val MODE_SKIP_SCREENSHOT = 0

    const val MODE_BOTH = 1

    fun apply(view: View, on: Boolean): Boolean {

        if (!view.isAttachedToWindow) {
            log("view 尚未 attach，排队等 attach 后打标 (on=$on)")
            view.addOnAttachStateChangeListener(object : android.view.View.OnAttachStateChangeListener {
                override fun onViewAttachedToWindow(v: View) {
                    v.removeOnAttachStateChangeListener(this)

                    v.post { applyNow(v, on) }
                }
                override fun onViewDetachedFromWindow(v: View) = Unit
            })
            return true
        }
        return applyNow(view, on)
    }

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

            val cause = e.cause ?: e
            log("反射失败: ${cause.javaClass.simpleName}: ${cause.message}")
            false
        }
    }

    private fun surfaceControlOf(view: View): Any? {

        run {
            try {
                val root = view.rootSurfaceControl
                if (root != null) {
                    val sc = fieldByName(root, "mSurfaceControl")
                        ?: fieldByTypeName(root, "SurfaceControl")
                    if (sc != null) return sc
                }
            } catch (e: Throwable) {
                log("路径A(getRootSurfaceControl) 失败: ${e.javaClass.simpleName}")
            }
        }

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
