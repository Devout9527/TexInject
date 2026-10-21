package com.kael.texinject.ui

import android.view.View
import java.lang.reflect.Field

object SkipScreenshot {

    private const val TAG = "[TexInject/SkipScreenshot]"

    const val MODE_SKIP_SCREENSHOT = 0

    const val MODE_BOTH = 1

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

    private fun surfaceControlOf(view: View): Any? {
        val attachInfo = fieldByName(view, "mAttachInfo")
            ?: fieldByTypeName(view, "AttachInfo")
            ?: run { log("找不到 AttachInfo"); return null }

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
