/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui.compose

import android.content.Context
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.savedstate.SavedStateRegistry
import androidx.savedstate.SavedStateRegistryController
import androidx.savedstate.SavedStateRegistryOwner

/**
 * 让 Compose 能在**非 Compose 宿主**里跑起来。
 *
 * 场景：模块的大部分 UI 是注入到**游戏进程/游戏 Activity**里的
 * （见 OverlayManager：独立 PopupWindow；以及各 PopupWindow 弹窗）。
 * 游戏的 Activity 不是 [androidx.activity.ComponentActivity]，
 * 树上没有 LifecycleOwner / ViewModelStoreOwner / SavedStateRegistryOwner，
 * 直接 `ComposeView.setContent {}` 会因为拿不到这些 owner 而崩溃。
 *
 * 做法：自己造一个「三合一 owner」，挂到视图树上，
 *   ViewTreeLifecycleOwner / ViewTreeViewModelStoreOwner / ViewTreeSavedStateRegistryOwner
 * 之后子树里的任何 ComposeView 都能正常工作。
 */
class ComposeOwner : LifecycleOwner, ViewModelStoreOwner, SavedStateRegistryOwner {

    private var started = false
    private val lifecycleRegistry = LifecycleRegistry(this)
    private val savedStateController = SavedStateRegistryController.create(this)

    override val lifecycle: Lifecycle get() = lifecycleRegistry
    override val viewModelStore: ViewModelStore = ViewModelStore()
    override val savedStateRegistry: SavedStateRegistry get() = savedStateController.savedStateRegistry

    /**
     * 把 owner 绑定到某个 View 上（可以多次调用，绑多个 View）。
     *
     * 生命周期初始化只会执行一次。
     */
    fun installOn(host: View): ComposeOwner {
        if (!started) {
            started = true
            savedStateController.performAttach()
            savedStateController.performRestore(null)
            lifecycleRegistry.currentState = Lifecycle.State.RESUMED
        }
        bindOwner(host, "androidx.lifecycle.ViewTreeLifecycleOwner", this)
        bindOwner(host, "androidx.lifecycle.ViewTreeViewModelStoreOwner", this)
        bindOwner(host, "androidx.savedstate.ViewTreeSavedStateRegistryOwner", this)
        return this
    }

    companion object {
        /**
         * 用反射挂 ViewTree owner。
         *
         * 这些类在编译期解析不到（lifecycle/savedstate 的 KMP 变体拆分导致
         * classes.jar 没进 compileClasspath），但运行时**一定存在** ——
         * 因为 ComposeView 自己内部就要用它们。所以统一走反射。
         */
        fun bindOwner(view: View, ownerClass: String, owner: Any) {
            try {
                val cls = Class.forName(ownerClass)
                val m = cls.methods.firstOrNull {
                    it.name == "set" && it.parameterCount == 2 && it.parameterTypes[0] == View::class.java
                } ?: return
                m.invoke(null, view, owner)
            } catch (_: Throwable) {
            }
        }
    }

    fun destroy() {
        lifecycleRegistry.currentState = Lifecycle.State.DESTROYED
        viewModelStore.clear()
    }
}

object ComposeHost {

    /**
     * 造一个已经能用的 [ComposeView]。
     *
     * @param hostView 视图树上的任意一个 View（通常是弹窗/悬浮层的根 View），
     *                 owner 会挂到它上面；ComposeView 必须是它的子孙或它自己。
     */
    fun create(
        context: Context,
        hostView: View,
        content: @Composable () -> Unit
    ): ComposeView {
        val owner = ComposeOwner().installOn(hostView)
        return ComposeView(context).apply {
            setViewCompositionStrategy(
                ViewCompositionStrategy.DisposeOnDetachedFromWindowOrReleasedFromPool
            )
            setContent {
                TexInjectTheme { content() }
            }
            // 让 ComposeView 自己能拿到 owner
            ComposeOwner.bindOwner(this, "androidx.lifecycle.ViewTreeLifecycleOwner", owner)
            ComposeOwner.bindOwner(this, "androidx.lifecycle.ViewTreeViewModelStoreOwner", owner)
            ComposeOwner.bindOwner(this, "androidx.savedstate.ViewTreeSavedStateRegistryOwner", owner)
        }
    }
}


/**
 * 把 Compose 内容当作一个普通 View 用。
 *
 * 因为宿主（悬浮面板 / 弹窗）在被 addView 之前还没进视图树，
 * 拿不到 ViewTree owner，所以把 owner 的安装推迟到 [onAttachedToWindow]。
 */
class ComposeHostView(
    context: Context,
    private val content: @Composable () -> Unit
) : android.widget.FrameLayout(context) {

    private var owner: ComposeOwner? = null

    /** 我们自己挂的 ComposeView。只清它，别动外部挂进来的 View。 */
    private var composeView: ComposeView? = null

    /**
     * 返回「当前可触摸的区域」（屏幕坐标）。其余区域的触摸会回落到下层游戏窗口。
     * 悬浮层是全屏独立窗口，不声明这个的话会吞掉游戏的所有触摸。
     */
    var touchRegionProvider: (() -> android.graphics.Rect?)? = null

    /** 触摸穿透是否注册成功（失败的话模块 UI 会点不动）。 */
    @Volatile
    var passthroughOk: Boolean = false
        private set

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (owner != null) return
        val o = ComposeOwner()
        // 关键：Compose 在 AbstractComposeView.resolveParentCompositionContext()
        // 里是拿 view.rootView（PopupWindow 就是 PopupDecorView）去找
        // ViewTreeLifecycleOwner 的，挂在中间层够不到，必须挂到窗口根视图。
        o.installOn(rootView)
        o.installOn(this)
        owner = o
        // 注意：这里不能 removeAllViews()。
        // NowPlayingBar / HotkeyLayer 是在 attach 之前就挂到本容器的，
        // 一并清掉会导致播放条和快捷键层直接消失。
        // 只移除上次我们自己的 ComposeView，防止重复挂载。
        composeView?.let { removeView(it) }
        val cv = ComposeView(context).apply {
            setViewCompositionStrategy(
                ViewCompositionStrategy.DisposeOnDetachedFromWindowOrReleasedFromPool
            )
            setContent {
                TexInjectTheme { content() }
            }
        }
        composeView = cv
        // 插到最底层，让外部挂进来的播放条/快捷键层盖在上面
        addView(cv, 0, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        installTouchPassthrough()
        // 决定性诊断：RenderEffect（模糊/液态玻璃）在非硬件加速的窗口里会静默失效
        log(
            "overlay attached: isHardwareAccelerated=${isHardwareAccelerated}, " +
                "api=${android.os.Build.VERSION.SDK_INT}"
        )
    }

    /** 用反射注册 OnComputeInternalInsetsListener（API 30 起该 API 已从 SDK 移除）。 */
    private fun installTouchPassthrough() {
        try {
            val listenerCls = Class.forName("android.view.ViewTreeObserver\$OnComputeInternalInsetsListener")
            val proxy = java.lang.reflect.Proxy.newProxyInstance(
                listenerCls.classLoader, arrayOf(listenerCls)
            ) { _, method, args ->
                if (method.name == "onComputeInternalInsets" && args != null && args.isNotEmpty()) {
                    try {
                        val info = args[0]
                        val cls = info.javaClass
                        val region = cls.getField("touchableRegion").get(info) as android.graphics.Region
                        region.setEmpty()
                        val r = touchRegionProvider?.invoke()
                        if (r != null) {
                            region.set(r)
                        } else {
                            log("touchRegionProvider 返回 null（可触摸区域为空 -> 点不动）")
                        }
                        // TOUCHABLE_INSETS_REGION = 3
                        cls.getMethod("setTouchableInsets", Int::class.javaPrimitiveType)
                            .invoke(info, 3)
                    } catch (_: Throwable) {
                    }
                }
                null
            }
            val vto = viewTreeObserver
            vto.javaClass.getMethod("addOnComputeInternalInsetsListener", listenerCls)
                .invoke(vto, proxy)
            passthroughOk = true
            log("touch passthrough installed")
        } catch (e: Throwable) {
            log("touch passthrough FAILED: ${e.javaClass.simpleName}: ${e.message}")
        }
    }

    private fun log(msg: String) {
        try {
            com.kael.texinject.TexInjectHookInit.logExternal("[TexInject/ComposeHost] $msg")
        } catch (_: Throwable) {
        }
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        owner?.destroy()
        owner = null
        composeView?.let { removeView(it) }
        composeView = null
    }
}
