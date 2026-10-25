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

object DevMode {

    private const val PREF = "texinject_dev"
    private const val KEY_SHOW_BOUNDS = "show_bounds"

    private const val TAP_WINDOW_MS = 1200L
    private const val TAP_NEEDED = 3

    private var appCtx: Context? = null
    private val state = mutableStateOf(false)

    private var taps = 0
    private var firstTapAt = 0L

    val enabled: Boolean get() = state.value

    private fun prefs(c: Context) =
        c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

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

private val DEV_LINE = Color(0xFFFF3B30)

private val DEV_LINE_SOFT = Color(0x66FF3B30)

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

@Composable
fun DevBoundsOverlay() {
    if (!DevMode.enabled) return
    val view = LocalView.current
    val strokePx = with(LocalDensity.current) { 1.dp.toPx() }

    var rects by remember { mutableStateOf<List<Rect>>(emptyList()) }
    var rootOk by remember { mutableStateOf(false) }

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

internal object DevBounds {

    private const val MAX_NODES = 1500
    private const val MAX_DEPTH = 64

    private val buf = ArrayList<Rect>(512)

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
            val decor = composeView.rootView
            if (decor != null) {
                val origin = IntArray(2)
                composeView.getLocationInWindow(origin)
                walkView(decor, origin[0].toFloat(), origin[1].toFloat(), 0)
            }
        } catch (_: Throwable) {
        }
        return buf
    }

    private fun add(r: Rect) {
        if (buf.size < MAX_NODES) buf.add(r)
    }

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

    private fun walkView(v: View, ox: Float, oy: Float, depth: Int) {
        if (depth > 40 || buf.size >= MAX_NODES) return
        if (v.visibility != View.VISIBLE) return
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
