/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 */
package com.kael.texinject.ui.compose

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.widget.Toast
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asComposeRenderEffect
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import android.widget.FrameLayout
import com.kael.texinject.ui.Brand
import com.kael.texinject.GamePaths
import com.kael.texinject.LoginVideo
import com.kael.texinject.MusicPlayer
import com.kael.texinject.TexInjector
import com.kael.texinject.ui.CategoryIcons
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.key
import com.kael.texinject.ui.McpBridge
import com.kael.texinject.ui.McpModule
import com.kael.texinject.ui.OverlayManager
import com.kael.texinject.ui.FeatureConfigDialog
import com.kael.texinject.ui.HotkeyUi
import com.kael.texinject.ui.CenterCropVideo
import com.kael.texinject.ui.PanelBackground
import com.kael.texinject.ui.glass.GlassStyle
import com.kael.texinject.ui.glass.LiquidGlass
import com.kael.texinject.ui.PyLoaderUi
import kotlin.math.roundToInt

private val Accent = Color(0xFF0062FF)
private val PanelBg = Color(0xF0252525)
private val SidebarBg = Color(0x1AFFFFFF)
private val CardBg = Color(0x1AFFFFFF)

/** 分类名（索引与后端逻辑一致：0 材质 / 1 音乐 / 2 脚本 / 3 快捷键 / 4 设置）。 */
private val CATS = listOf("材质替换", "音乐", "脚本", "快捷键", "设置")

/** 悬浮层的「可触摸区域」，由 PanelOverlay 实时更新，供 ComposeHostView 读取。 */
object PanelTouchState {
    @Volatile
    var region: android.graphics.Rect? = null

    /** 悬浮球的屏幕矩形（关闭面板时的可触摸区域）。 */
    @Volatile
    var lastBallRect: android.graphics.Rect? = android.graphics.Rect(16, 64, 56, 104)
}

/**
 * 面板整体（Compose 版）。
 *
 * @param previewMode 桌面入口预览：不挂悬浮球、不加载任何注入功能。
 */
@Composable
fun PanelOverlay(activity: Activity, previewMode: Boolean = false) {
    var panelOpen by remember { mutableStateOf(previewMode) }
    // 面板开着时让悬浮窗可聚焦，否则输入法连不上（只有光标没有键盘）
    LaunchedEffect(panelOpen) { OverlayManager.setPanelFocusable(panelOpen) }
    val prefs = remember {
        activity.getSharedPreferences("texinject_overlay", Context.MODE_PRIVATE)
    }
    var cat by remember { mutableIntStateOf(prefs.getInt("cat", 0).coerceIn(0, CATS.size - 1)) }
    var rootSize by remember { mutableStateOf(IntSize.Zero) }
    // 横竖屏切换：根尺寸变了，触摸区必须重算
    LaunchedEffect(rootSize.width, rootSize.height) { OverlayManager.refreshTouchRegion() }

    // 触摸区域随「面板开合 / 悬浮球位置 / 根尺寸」实时重算
    val hostView = androidx.compose.ui.platform.LocalView.current
    SideEffect {
        PanelTouchState.region = if (panelOpen) {
            android.graphics.Rect(0, 0, rootSize.width, rootSize.height)
        } else {
            PanelTouchState.lastBallRect
        }
        // 关键：区域变了必须让窗口重算 insets。
        // onComputeInternalInsets 只在系统做 insets 计算时触发，
        // 不主动请求的话窗口会一直用旧的可触摸区域 ——
        // 表现为「悬浮球挪了位置后再点没反应」。
        hostView.requestApplyInsets()
    }

    Box(
        Modifier
            .fillMaxSize()
            .onGloballyPositioned { coords -> rootSize = coords.size }
    ) {
        if (!previewMode) {
            // 悬浮球改用原生 View（NativeBall.attach 在 OverlayManager 里挂）
            // 这里只把「开合」回调交给它，避免 Compose 画的球和触摸区对不上。
            androidx.compose.runtime.DisposableEffect(Unit) {
                com.kael.texinject.ui.PanelBridge.toggle = { panelOpen = !panelOpen }
                onDispose { com.kael.texinject.ui.PanelBridge.toggle = null }
            }
        }
        if (panelOpen) {
            PanelCard(
                activity = activity,
                selected = cat,
                onSelect = {
                    cat = it
                    prefs.edit().putInt("cat", it).apply()
                },
                onClose = { if (!previewMode) panelOpen = false }
            )
        }
    }
}

@Composable
private fun FloatingBall(activity: Activity, rootSize: IntSize, onClick: () -> Unit) {
    val density = LocalDensity.current
    val sizePx = with(density) { 40.dp.toPx() }
    val prefs = remember {
        activity.getSharedPreferences("texinject_overlay", Context.MODE_PRIVATE)
    }
    var x by remember { mutableFloatStateOf(prefs.getInt("x", 16f.let { with(density) { 16.dp.toPx() } }.toInt()).toFloat()) }
    var y by remember { mutableFloatStateOf(prefs.getInt("y", 64f.let { with(density) { 64.dp.toPx() } }.toInt()).toFloat()) }
    var moved by remember { mutableStateOf(false) }

    remember(x, y) { rememberBallRect(x, y, sizePx) }

    Box(
        Modifier
            .offset { IntOffset(x.roundToInt(), y.roundToInt()) }
            .size(40.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xCC1E1E22))
            .pointerInput(Unit) {
                detectDragGestures(
                    onDragStart = { moved = false },
                    onDrag = { change, drag ->
                        change.consume()
                        moved = true
                        val maxX = (rootSize.width - sizePx).coerceAtLeast(0f)
                        val maxY = (rootSize.height - sizePx).coerceAtLeast(0f)
                        x = (x + drag.x).coerceIn(0f, maxX)
                        y = (y + drag.y).coerceIn(0f, maxY)
                    },
                    onDragEnd = {
                        // 记住位置（旧代码存在 texinject_overlay 的 x/y）
                        prefs.edit().putInt("x", x.toInt()).putInt("y", y.toInt()).apply()
                        // 关键：拖动结束后必须复位，否则 clickable(enabled = !moved) 永久失效，
                        // 表现为「拖一次球以后再也点不开面板」。
                        moved = false
                        OverlayManager.refreshTouchRegion()
                    },
                    onDragCancel = {
                        moved = false
                        OverlayManager.refreshTouchRegion()
                    }
                )
            }
            .clickable(enabled = !moved) { onClick() },
        contentAlignment = Alignment.Center
    ) {
        val ballVideo = remember { com.kael.texinject.ui.IconLoader.findBallVideo(activity, GamePaths.sourceDir) }
        if (ballVideo != null) {
            AndroidView(
                modifier = Modifier.fillMaxSize(),
                factory = { ctx ->
                    val holder = FrameLayout(ctx)
                    CenterCropVideo(ctx).apply {
                        attach(holder)
                        play(ballVideo.absolutePath)
                    }
                    holder
                }
            )
        } else if (IconLogo.get(activity) != null) {
            val bmp = IconLogo.get(activity)!!
            Image(
                bitmap = bmp.asImageBitmap(),
                contentDescription = null,
                contentScale = ContentScale.Fit,
                modifier = Modifier.fillMaxSize().padding(4.dp)
            )
        } else {
            Text("T", color = Color.White, fontWeight = FontWeight.Bold)
        }
    }
}

/** 记录悬浮球的屏幕矩形（可触摸区域）。 */
private fun rememberBallRect(x: Float, y: Float, sizePx: Float) {
    val r = PanelTouchState.lastBallRect ?: return
    val nx = x.roundToInt(); val ny = y.roundToInt()
    val nx2 = (x + sizePx).roundToInt(); val ny2 = (y + sizePx).roundToInt()
    // 位置没变就不用惊动窗口（避免每帧一次 traversal）
    if (r.left == nx && r.top == ny && r.right == nx2 && r.bottom == ny2) return
    r.set(nx, ny, nx2, ny2)
    // 位置变了要主动让窗口重算触摸区，否则 region 停在旧坐标 —— 表现为"球点不动"
    OverlayManager.refreshTouchRegion()
}

private object IconLogo {
    private var cache: android.graphics.Bitmap? = null
    fun get(activity: Activity): android.graphics.Bitmap? {
        cache?.let { return it }
        val b = com.kael.texinject.ui.IconLoader.getLogo(activity, GamePaths.sourceDir)
        cache = b
        return b
    }
}

@Composable
private fun PanelCard(
    activity: Activity,
    selected: Int,
    onSelect: (Int) -> Unit,
    onClose: () -> Unit
) {
    // 面板布局参数固定，不再由用户自定义（取值即 PanelTuning 原本的默认值）
    val pad = 10.dp
    val iconDp = 20.dp
    val headerH = 40
    val dividerH = 1
    // 面板尺寸（设置页可调）；读 revision 让滑块变化能即时重组
    PanelSize.revision
    val bodyH = PanelSize.heightDp(activity)
    val panelW = PanelSize.widthDp(activity)
    val gstyle = remember { GlassStyle.get(activity) }
    // 快捷键页点 ⚙ 后，右栏整体切成配置菜单（不弹新窗口）
    var hkCfg by remember { mutableStateOf<McpModule?>(null) }
    val density = LocalDensity.current

    Box(
        Modifier
            .fillMaxSize()
            .background(Color(0x59000000))
            .clickable { onClose() },
        contentAlignment = Alignment.Center
    ) {
        // 背景层用哪种玻璃效果
        // ⚠️ RenderEffect 必须缓存！
        // LiquidGlass.effect() 内部会 new RuntimeShader(AGSL) —— 着色器编译很贵。
        // 这个 lambda 会被反复执行（尺寸变化 / 重组），若每次重建着色器，
        // 会把游戏 UI 线程（与游戏主线程同一个）占满 -> 输入 5s 无响应 -> ANR。
        val glassCache = remember { arrayOfNulls<Any>(2) }   // [key, RenderEffect]
        val glassEffect = Modifier.graphicsLayer {
            val c = with(density) { 16.dp.toPx() }
            val k = GlassStyle.liquidStrength(activity)
            val rh = GlassStyle.refractionDp(activity)
            val am = GlassStyle.amountDp(activity)
            val disp = GlassStyle.dispersion(activity)
            val blur = GlassStyle.blurRadiusDp(activity)
            val key = "$gstyle|${size.width}|${size.height}|$c|$k|$rh|$am|$disp|$blur"
            if (glassCache[0] != key) {
                glassCache[0] = key
                val eff: android.graphics.RenderEffect? = when (gstyle) {
                    GlassStyle.LIQUID -> if (rh > 0 && am > 0) {
                        LiquidGlass.effect(
                            size.width, size.height, c,
                            refractionPx = rh * k,
                            amountPx = am * k,
                            dispersion = disp / 100f * k,
                        )
                    } else null
                    GlassStyle.BLUR -> {
                        val r = with(density) { blur.dp.toPx() }
                        if (r > 0f) android.graphics.RenderEffect
                            .createBlurEffect(r, r, android.graphics.Shader.TileMode.CLAMP)
                        else null
                    }
                    else -> null
                }
                glassCache[1] = eff
            }
            @Suppress("UNCHECKED_CAST")
            (glassCache[1] as? android.graphics.RenderEffect)?.let {
                renderEffect = it.asComposeRenderEffect()
            }
        }

        val cfg = activity.resources.configuration
        val cardW = minOf(panelW.dp, cfg.screenWidthDp.dp * 0.94f)
        val cardHMax = cfg.screenHeightDp.dp * 0.91f

        Box(
            Modifier
                .width(cardW)
                .heightIn(max = cardHMax)
                .clip(RoundedCornerShape(16.dp))
                .background(Color(0x33000000))
                // 消费掉落在卡片上的点击，避免穿透到后面的遮罩把面板关掉
                .pointerInput(Unit) { detectTapGestures(onTap = {}) }
        ) {
            // ---- 卡片背景层 ----
            // 面板背景图：居中裁剪铺满整张卡片（不拉伸、不留黑边）。
            // 液态玻璃折射作用在【这一层】上 —— 有纹理才折射得出来。
            // 移植自备份 OverlayLayout：每次打开面板都 clearCache 再读一次，
            // 否则 remember{} 不带 key 只算一次，换了背景图必须重启游戏才生效。
            // PanelCard 只在面板打开时组合，所以进入即刷新一次；
            // 用 Unit 作 key，不能引用外层的 panelOpen（不在本函数作用域）。
            val bgTick = remember { androidx.compose.runtime.mutableIntStateOf(0) }
            androidx.compose.runtime.LaunchedEffect(Unit) {
                PanelBackground.clearCache()
                bgTick.intValue++
            }
            val bgVideo = remember(bgTick.intValue) {
                PanelBackground.findVideo(activity, GamePaths.sourceDir)
            }
            val bgBmp = remember(bgTick.intValue) {
                PanelBackground.get(activity, GamePaths.sourceDir)
            }
            if (bgVideo != null) {
                // 背景视频：TextureView + MediaPlayer 本身就会创建硬件层
                // (getHardwareLayer/createNewSurface)，外面再套 graphicsLayer{renderEffect}
                // 会在视频帧刷新时崩溃(SIGSEGV)。所以视频层永远不套 RenderEffect。
                AndroidView(
                    modifier = Modifier.matchParentSize(),
                    factory = { ctx ->
                        val holder = FrameLayout(ctx)
                        CenterCropVideo(ctx).apply {
                            attach(holder)
                            play(bgVideo.absolutePath)
                        }
                        holder
                    }
                )
            } else if (bgBmp != null) {
                Image(
                    bitmap = bgBmp.asImageBitmap(),
                    contentDescription = null,
                    contentScale = androidx.compose.ui.layout.ContentScale.Crop,
                    modifier = Modifier
                        .matchParentSize().then(glassEffect)
                )
            } else {
                Box(
                    Modifier
                        .matchParentSize()
                        .background(PanelBg)
                        .then(glassEffect)
                )
            }
            // ---- 边缘高光层（玻璃质感主要靠它，BlendMode.Plus 叠加）----
            val hlCache = remember { arrayOfNulls<Any>(2) }
            if (GlassStyle.LIQUID_GLASS_ENABLED && gstyle == GlassStyle.LIQUID && GlassStyle.highlight(activity)) {
                Box(
                    Modifier
                        .matchParentSize()
                        .graphicsLayer {
                            // 同样必须缓存：LiquidGlass.highlight() 内部也 new RuntimeShader
                            val c2 = with(density) { 16.dp.toPx() }
                            val fo = GlassStyle.highlightFalloff(activity)
                            val key = "hl|${size.width}|${size.height}|$c2|$fo"
                            if (hlCache[0] != key) {
                                hlCache[0] = key
                                hlCache[1] = LiquidGlass.highlight(
                                    size.width, size.height, c2,
                                    color = 0xFFFFFFFF.toInt(),
                                    falloff = fo / 10f,
                                )
                            }
                            @Suppress("UNCHECKED_CAST")
                            (hlCache[1] as? android.graphics.RenderEffect)?.let {
                                renderEffect = it.asComposeRenderEffect()
                            }
                        }
                )
            }

            Column(Modifier.fillMaxWidth().padding(pad)) {
            // 头部
            Text(
                Brand.title(activity),
                fontSize = 16.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White,
                modifier = Modifier
                    .then(if (headerH > 0) Modifier.heightIn(min = headerH.dp) else Modifier)
                    .padding(bottom = 8.dp)
            )
            if (dividerH > 0) {
                Box(
                    Modifier
                        .fillMaxWidth()
                        .height(dividerH.dp)
                        .background(Color(0x33FFFFFF))
                )
            }

            // 高度再被 82% 屏高兜底，避免调太大超出屏幕
            val bodyMax = minOf(bodyH.dp, cardHMax - 80.dp)
            // 固定高度：原生 View(音乐/脚本/快捷键) 必须有确定高度才会自己滚动，
            // 否则会按内容撑开面板 —— 表现为「列表滑不动」和「执行脚本后面板缩小一截」。
            Row(
                Modifier
                    .fillMaxWidth()
                    .height(bodyMax)
                    .padding(top = 8.dp)
            ) {
                // 左栏
                Column(
                    Modifier
                        .width(96.dp)
                        // 撑满整行高度：否则内容少时左栏背景只包住内容，
                        // 面板调高后底下会露出一段空白。内容超出时仍可滚动。
                        .fillMaxHeight()
                        .clip(RoundedCornerShape(12.dp))
                        .background(SidebarBg)
                        .padding(4.dp)
                ) {
                    CATS.forEachIndexed { i, name ->
                        val bmp = remember(i) {
                            CategoryIcons.get(activity, GamePaths.sourceDir, name, i)
                        }
                        Row(
                            Modifier
                                .fillMaxWidth()
                                // 均分左栏高度：面板调高后各项一起变高，不会只剩底部一段空白
                                .weight(1f)
                                .clip(RoundedCornerShape(9.dp))
                                .background(if (i == selected) Accent else Color.Transparent)
                                .clickable { onSelect(i) }
                                .padding(horizontal = 6.dp, vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            if (bmp != null) {
                                Image(
                                    bitmap = bmp.asImageBitmap(),
                                    contentDescription = null,
                                    modifier = Modifier.size(iconDp)
                                )
                            }
                            Text(name, fontSize = 12.sp, color = Color.White)
                        }
                    }
                }
                Spacer(Modifier.width(8.dp))
                // 右栏内容
                Column(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .clip(RoundedCornerShape(12.dp))
                        .background(CardBg)
                        .padding(10.dp)
                ) {
                    when (selected) {
                        0 -> Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                            MaterialTab(activity)
                        }
                        1 -> AndroidView({ MusicPlayer.buildView(activity) }, Modifier.fillMaxSize())
                        2 -> AndroidView({ PyLoaderUi.buildView(activity) }, Modifier.fillMaxSize())
                        3 -> {
                            val cfg = hkCfg
                            if (cfg != null) {
                                // 打开某个模块的配置：先让 Python 侧把【这个模块】的配置定义吐出来，
                                // 否则 loadConfig() 读到的是上一个模块的缓存（点扳机却显示上一个）。
                                var cfgGen by remember(cfg) { mutableIntStateOf(0) }
                                LaunchedEffect(cfg) {
                                    McpBridge.refreshConfig(activity, cfg.name)
                                    kotlinx.coroutines.delay(350)
                                    cfgGen++
                                }
                                Column(Modifier.fillMaxSize()) {
                                    // 关闭改用 Compose 按钮：原生 View 的 × 在这个叠加层里收不到点击
                                    Row(
                                        Modifier.fillMaxWidth().padding(bottom = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Text(
                                            "← 返回模块列表",
                                            fontSize = 13.sp,
                                            color = Color(0xFF7FB2FF),
                                            modifier = Modifier
                                                .weight(1f)
                                                .clickable { hkCfg = null }
                                        )
                                        Box(
                                            Modifier
                                                .size(26.dp)
                                                .clip(RoundedCornerShape(13.dp))
                                                .background(Color(0x33FFFFFF))
                                                .clickable { hkCfg = null },
                                            contentAlignment = Alignment.Center
                                        ) {
                                            Text("×", fontSize = 17.sp, color = Color.White)
                                        }
                                    }
                                    // 不套 verticalScroll：高度一旦无界，里面 weight(1f) 的列表会塌成 0
                                    key(cfg, cfgGen) {
                                        AndroidView(
                                            { FeatureConfigDialog.buildCard(activity, cfg) { hkCfg = null } },
                                            Modifier.fillMaxWidth().weight(1f)
                                        )
                                    }
                                }
                            } else {
                                AndroidView(
                                    { HotkeyUi.buildView(activity) { m -> hkCfg = m } },
                                    Modifier.fillMaxSize()
                                )
                            }
                        }
                        // 设置页是纯 Compose，自己套滚动；滚动位置记在 PanelScroll 里，关面板不丢
                        4 -> {
                            val st = rememberScrollState(PanelScroll.settings)
                            LaunchedEffect(st.value) { PanelScroll.settings = st.value }
                            Column(Modifier.fillMaxSize().verticalScroll(st)) {
                                if (PanelScroll.cookiePage) {
                                    CookieScreen(activity) { PanelScroll.cookiePage = false }
                                } else {
                                    SettingsScreen(activity) { PanelScroll.cookiePage = true }
                                }
                            }
                        }
                    }
                }
            }
            }
        }
    }
}

/** 材质替换分类的动作按钮。 */
@Composable
private fun MaterialTab(activity: Activity) {
    val main = Handler(Looper.getMainLooper())
    fun toast(msg: String) = main.post { Toast.makeText(activity, msg, Toast.LENGTH_SHORT).show() }

    val actions = buildList<Pair<String, () -> String>> {
        if (!GamePaths.isTestBuild) {
            add("注入资源" to { TexInjector.injectAll(activity, GamePaths.sourceDir).summary })
            add("恢复官方" to { TexInjector.restore(activity, GamePaths.sourceDir).summary })
        }
        add("替换视频" to { LoginVideo.replace(activity, GamePaths.sourceDir).summary })
        add("恢复视频" to { LoginVideo.restore(activity).summary })
        add("重启游戏" to {
            main.postDelayed({ Process.killProcess(Process.myPid()) }, 600)
            "即将退出游戏"
        })
    }

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        actions.forEach { (label, run) ->
            Box(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp))
                    .background(Color(0x33FFFFFF))
                    .clickable {
                        // 注入/还原动辄几千个文件、几十 MB md5 表，绝对不能在主线程跑，
                        // 否则直接 ANR/被系统杀进程（表现就是"一点就崩"）。
                        toast("$label …")
                        Thread({
                            val msg = runCatching { run() }.getOrElse { it.message ?: "失败" }
                            toast(msg)
                        }, "texinject-pack").start()
                    }
                    .padding(vertical = 12.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(label, fontSize = 13.sp, color = Color.White)
            }
        }
    }
}
