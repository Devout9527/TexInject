/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui.compose

import android.content.Context
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.layout.LayoutInfo
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay

/**
 * 开发者模式（隐藏开关）。
 *
 * 入口：主面板左上角标题「TexInject · …」连续点 3 下。
 * 开启后，面板里**每个布局节点的触摸范围**都会用红线描出来：
 *   - Compose 侧：反射取到 ComposeView 的根 LayoutNode，再用公开的
 *     [LayoutInfo] 接口递归遍历（不碰任何内部 API 的坐标计算）；
 *   - 原生侧：同一窗口里 Compose 之外的 View（播放条、悬浮胶囊等）
 *     按窗口坐标换算到 Compose 根坐标后一起画。
 *
 * 再点 3 下关闭。状态存 SharedPreferences，游戏重启后保持。
 */
object DevMode {

    private const val PREF = "texinject_dev"
    private const val KEY_SHOW_BOUNDS = "show_bounds"

    /** 连点判定窗口：超过这么久没点就重新计数。 */
    private const val TAP_WINDOW_MS = 1200L
    private const val TAP_NEEDED = 3

    private var appCtx: Context? = null
    private val state = mutableStateOf(false)

    private var taps = 0
    private var firstTapAt = 0L

    val enabled: Boolean get() = state.value

    private fun prefs(c: Context) =
        c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    /** 首次读取时从 SP 恢复。可重复调用。 */
    fun ensure(c: Context) {
        if (appCtx != null) return
        val app = c.applicationContext ?: c
        appCtx = app
        state.value = prefs(app).getBoolean(KEY_SHOW_BOUNDS, false)
    }

    fun set(c: Context, on: Boolean) {
        ensure(c)
        state.value = on
        prefs(appCtx ?: c).edit().putBoolean(KEY_SHOW_BOUNDS, on).apply()
    }

    /**
     * 标题被点了一下。
     * @return true 表示这次点击刚好把模式**切换**了（调用方负责弹提示）。
     */
    fun onTitleTap(c: Context): Boolean {
        ensure(c)
        val now = SystemClock.uptimeMillis()
        if (taps == 0 || now - firstTapAt > TAP_WINDOW_MS) {
            taps = 0
            firstTapAt = now
        }
        taps++
        if (taps < TAP_NEEDED) return false
        taps = 0
        set(c, !state.value)
        return true
    }
}

/** 手写标注用的红：不透明，最显眼。 */
private val DEV_LINE = Color(0xFFFF3B30)

/** 自动遍历出来的红：半透明，作为补充信息，别盖住手写框。 */
private val DEV_LINE_SOFT = Color(0x66FF3B30)

/**
 * 给一个容器画一圈红框（只在开发者模式开启时可见）。
 *
 * 用 [drawWithContent]：绘制发生在**这个元素自己的绘制阶段**，所以
 *   - 不影响布局（不像 border 那样占位）
 *   - 不参与命中测试（不会挡住任何一个点击）
 *   - 不依赖反射 / Compose 内部 API —— R8 混淆也照样画得出来
 *
 * 加在 modifier 链的最后，框就画在背景和内容之上。
 */
fun Modifier.devBox(): Modifier = drawWithContent {
    drawContent()
    if (DevMode.enabled) {
        drawRect(
            color = DEV_LINE,
            size = size,
            style = Stroke(width = 1.dp.toPx())
        )
    }
}

/**
 * 盖在界面最上层的一层"描边"。
 *
 * 不响应任何触摸事件（纯绘制，没有 pointerInput），所以**不会**改变
 * 底下控件的点击行为 —— 它只是把 hit 边界画出来给你看。
 *
 * 主要来源是各容器自己的 [devBox]；这里额外用反射把「没手动标注的节点」
 * 和同窗口的原生 View 也扫一遍，画成淡红细框。
 * 右上角会显示扫到的框数量 —— 反射被 R8 干掉时会显示 `(no root)`。
 */
@Composable
fun DevBoundsOverlay() {
    if (!DevMode.enabled) return
    val view = LocalView.current
    val strokePx = with(LocalDensity.current) { 1.dp.toPx() }

    var rects by remember { mutableStateOf<List<Rect>>(emptyList()) }
    var rootOk by remember { mutableStateOf(false) }

    // 拖动/滚动/换页时布局会变，定时重新收集（不需要每帧，省电也省 CPU）
    LaunchedEffect(Unit) {
        while (true) {
            rects = DevBounds.collect(view).toList()
            rootOk = DevBounds.rootOk
            delay(150)
        }
    }

    Box(Modifier.fillMaxSize()) {
        Canvas(Modifier.fillMaxSize()) {
            for (r in rects) {
                if (r.width <= 0f || r.height <= 0f) continue
                // 完全在视野外的就别画了
                if (r.right < 0f || r.bottom < 0f) continue
                if (r.left > size.width || r.top > size.height) continue
                drawRect(
                    color = DEV_LINE_SOFT,
                    topLeft = r.topLeft,
                    size = r.size,
                    style = Stroke(width = strokePx)
                )
            }
        }
        // 诊断角标：自动扫到的框数量（0 + no root 说明反射被混淆掉了）
        Text(
            text = "DEV ${rects.size}" + if (rootOk) "" else " (no root)",
            fontSize = 10.sp,
            color = DEV_LINE,
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(6.dp)
                .background(Color(0x99000000), RoundedCornerShape(4.dp))
                .padding(horizontal = 6.dp, vertical = 3.dp)
        )
    }
}

/**
 * 收集当前窗口里所有"可触摸矩形"（Compose 根坐标系）。
 *
 * 复用同一个 ArrayList，避免 150ms 一次地反复分配。
 */
internal object DevBounds {

    private const val MAX_NODES = 1500
    private const val MAX_DEPTH = 64

    private val buf = ArrayList<Rect>(512)

    /** 上一次是否成功拿到 Compose 根节点（诊断用）。 */
    @Volatile
    var rootOk: Boolean = false
        private set

    fun collect(anyView: View): List<Rect> {
        buf.clear()
        rootOk = false
        try {
            val composeView = findComposeView(anyView) ?: return buf
            rootLayoutInfo(composeView)?.let {
                rootOk = true
                walkNode(it, 0)
            }
            // 同窗口里的原生 View（Compose 树看不到它们）
            val decor = composeView.rootView
            if (decor != null) {
                val origin = IntArray(2)
                composeView.getLocationInWindow(origin)
                walkView(decor, origin[0].toFloat(), origin[1].toFloat(), 0)
            }
        } catch (_: Throwable) {
            // 开发者模式是调试功能，任何异常都不能影响界面
        }
        return buf
    }

    private fun add(r: Rect) {
        if (buf.size < MAX_NODES) buf.add(r)
    }

    /** Compose 节点：坐标直接用公开接口算，只有"拿根节点 / 拿子节点"这两步需要反射。 */
    private fun walkNode(n: LayoutInfo, depth: Int) {
        if (depth > MAX_DEPTH || buf.size >= MAX_NODES) return
        try {
            if (n.isPlaced) {
                val c = n.coordinates
                if (c.isAttached) {
                    val r = c.boundsInRoot()
                    if (r.width >= 1f && r.height >= 1f) add(r)
                }
            }
            for (child in childrenOf(n)) {
                (child as? LayoutInfo)?.let { walkNode(it, depth + 1) }
            }
        } catch (_: Throwable) {
        }
    }

    /**
     * `LayoutNode.children` 是 internal（JVM 名会带 `$ui_release` 后缀），
     * 只能反射取；反射结果按类缓存，避免每秒上千次查找。
     */
    private var childGetter: java.lang.reflect.Method? = null
    private var childGetterCls: Class<*>? = null

    private fun childrenOf(n: Any): List<*> {
        var m = childGetter
        if (m == null || childGetterCls != n.javaClass) {
            m = n.javaClass.methods.firstOrNull {
                it.parameterCount == 0 &&
                    it.returnType == List::class.java &&
                    (it.name == "getChildren" || it.name == "getChildren\$ui_release")
            }
            childGetter = m
            childGetterCls = n.javaClass
        }
        if (m == null) return emptyList<Any>()
        return try {
            m.invoke(n) as? List<*> ?: emptyList<Any>()
        } catch (_: Throwable) {
            emptyList<Any>()
        }
    }

    /** 原生 View：getLocationInWindow 减去 ComposeView 的窗口位置 = Compose 根坐标。 */
    private fun walkView(v: View, ox: Float, oy: Float, depth: Int) {
        if (depth > 40 || buf.size >= MAX_NODES) return
        if (v.visibility != View.VISIBLE) return
        // AndroidComposeView 自身的内容由上面的 walkNode 负责；
        // 但它的**子 View** 是 AndroidView 里塞进来的真原生控件，要继续扫。
        val isCompose = v.javaClass.name.contains("AndroidComposeView")
        if (!isCompose && v.width > 0 && v.height > 0) {
            val loc = IntArray(2)
            v.getLocationInWindow(loc)
            val l = loc[0] - ox
            val t = loc[1] - oy
            add(Rect(l, t, l + v.width, t + v.height))
        }
        if (v is ViewGroup) {
            for (i in 0 until v.childCount) walkView(v.getChildAt(i), ox, oy, depth + 1)
        }
    }

    private fun findComposeView(v: View): View? {
        if (v.javaClass.name.contains("AndroidComposeView")) return v
        if (v is ViewGroup) {
            for (i in 0 until v.childCount) {
                findComposeView(v.getChildAt(i))?.let { return it }
            }
        }
        return null
    }

    /**
     * 反射取 ComposeView（AndroidComposeView）的根 LayoutNode。
     *
     * 只有这一步依赖内部实现：`AndroidComposeView.root` 是 internal，
     * JVM 上方法名可能被 mangle 成 `getRoot$ui_release`，所以按前缀匹配
     * 所有返回 [LayoutInfo] 的无参 getRoot*。
     */
    private fun rootLayoutInfo(cv: View): LayoutInfo? {
        try {
            for (m in cv.javaClass.methods) {
                if (m.parameterCount != 0) continue
                val name = m.name
                if (name != "getRoot" && !name.startsWith("getRoot\$")) continue
                val r = try {
                    m.isAccessible = true
                    m.invoke(cv)
                } catch (_: Throwable) {
                    null
                }
                if (r is LayoutInfo) return r
            }
            // 兜底：直接读字段
            var c: Class<*>? = cv.javaClass
            while (c != null && c != View::class.java) {
                for (f in c.declaredFields) {
                    if (f.name != "root") continue
                    try {
                        f.isAccessible = true
                        val r = f.get(cv)
                        if (r is LayoutInfo) return r
                    } catch (_: Throwable) {
                    }
                }
                c = c.superclass
            }
        } catch (_: Throwable) {
        }
        return null
    }
}
