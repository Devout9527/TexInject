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

class ComposeOwner : LifecycleOwner, ViewModelStoreOwner, SavedStateRegistryOwner {

    private var started = false
    private val lifecycleRegistry = LifecycleRegistry(this)
    private val savedStateController = SavedStateRegistryController.create(this)

    override val lifecycle: Lifecycle get() = lifecycleRegistry
    override val viewModelStore: ViewModelStore = ViewModelStore()
    override val savedStateRegistry: SavedStateRegistry get() = savedStateController.savedStateRegistry

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
            ComposeOwner.bindOwner(this, "androidx.lifecycle.ViewTreeLifecycleOwner", owner)
            ComposeOwner.bindOwner(this, "androidx.lifecycle.ViewTreeViewModelStoreOwner", owner)
            ComposeOwner.bindOwner(this, "androidx.savedstate.ViewTreeSavedStateRegistryOwner", owner)
        }
    }
}

class ComposeHostView(
    context: Context,
    private val content: @Composable () -> Unit
) : android.widget.FrameLayout(context) {

    private var owner: ComposeOwner? = null

    private var composeView: ComposeView? = null

    var touchRegionProvider: (() -> android.graphics.Rect?)? = null

    @Volatile
    var passthroughOk: Boolean = false
        private set

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        if (owner != null) return
        val o = ComposeOwner()
        o.installOn(rootView)
        o.installOn(this)
        owner = o
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
        addView(cv, 0, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        installTouchPassthrough()
        log(
            "overlay attached: isHardwareAccelerated=${isHardwareAccelerated}, " +
                "api=${android.os.Build.VERSION.SDK_INT}"
        )
    }

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
