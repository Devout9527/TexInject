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
import com.kael.texinject.ResourceMount
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

private fun catsFor(): List<String> {
    return buildList {
        add("材质替换")
        add("音乐")
        add("脚本")
        add("快捷键")
        add("扩展")
        add("设置")
    }
}

object PanelTouchState {
    @Volatile
    var region: android.graphics.Rect? = null

}

@Composable
fun PanelOverlay(activity: Activity, previewMode: Boolean = false) {
    var panelOpen by remember { mutableStateOf(previewMode) }
    val prefs = remember {
        activity.getSharedPreferences("texinject_overlay", Context.MODE_PRIVATE)
    }
    val cats = remember { catsFor() }

    var cat by remember {
        mutableStateOf(prefs.getString("catName", cats.first())?.takeIf { it in cats } ?: cats.first())
    }
    var rootSize by remember { mutableStateOf(IntSize.Zero) }

    LaunchedEffect(rootSize.width, rootSize.height) { OverlayManager.refreshTouchRegion() }

    val hostView = androidx.compose.ui.platform.LocalView.current
    SideEffect {

        val hostW = hostView.width
        val hostH = hostView.height

        val waitForSize = panelOpen && hostW <= 0 && hostH <= 0 &&
            rootSize == androidx.compose.ui.unit.IntSize.Zero
        if (!waitForSize) PanelTouchState.region = if (panelOpen) {
            android.graphics.Rect(
                0, 0,
                if (hostW > 0) hostW else rootSize.width,
                if (hostH > 0) hostH else rootSize.height,
            )
        } else {

            null
        }

        hostView.requestApplyInsets()
    }

    remember { DevMode.ensure(activity) }

    Box(
        Modifier
            .fillMaxSize()
            .onGloballyPositioned { coords -> rootSize = coords.size }
    ) {
        if (!previewMode) {

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
                    prefs.edit().putString("catName", it).apply()
                },
                onClose = { if (!previewMode) panelOpen = false }
            )
        }

        DevBoundsOverlay()
    }
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
    selected: String,
    onSelect: (String) -> Unit,
    onClose: () -> Unit
) {

    val pad = 10.dp
    val iconDp = 20.dp

    val cats = remember { catsFor() }
    val headerH = 40
    val dividerH = 1

    PanelSize.revision
    val bodyH = PanelSize.heightDp(activity)
    val panelW = PanelSize.widthDp(activity)
    val gstyle = remember { GlassStyle.get(activity) }

    var hkCfg by remember { mutableStateOf<McpModule?>(null) }
    val density = LocalDensity.current

    Box(
        Modifier
            .fillMaxSize()
            .background(Color(0x59000000))
            .clickable { onClose() },
        contentAlignment = Alignment.Center
    ) {

        val glassCache = remember { arrayOfNulls<Any>(2) }
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

                .pointerInput(Unit) { detectTapGestures(onTap = {}) }
                .devBox()
        ) {

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

            val hlCache = remember { arrayOfNulls<Any>(2) }
            if (GlassStyle.LIQUID_GLASS_ENABLED && gstyle == GlassStyle.LIQUID && GlassStyle.highlight(activity)) {
                Box(
                    Modifier
                        .matchParentSize()
                        .graphicsLayer {

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

            Text(
                Brand.title(activity),
                fontSize = 16.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White,
                modifier = Modifier
                    .then(if (headerH > 0) Modifier.heightIn(min = headerH.dp) else Modifier)
                    .padding(bottom = 8.dp)
                    .pointerInput(Unit) {
                        detectTapGestures(onTap = {
                            if (DevMode.onTitleTap(activity)) {
                                Toast.makeText(
                                    activity,
                                    if (DevMode.enabled) "开发者模式：已开启（触摸范围）"
                                    else "开发者模式：已关闭",
                                    Toast.LENGTH_SHORT
                                ).show()
                            }
                        })
                    }
                    .devBox()
            )
            if (dividerH > 0) {
                Box(
                    Modifier
                        .fillMaxWidth()
                        .height(dividerH.dp)
                        .background(Color(0x33FFFFFF))
                        .devBox()
                )
            }

            val bodyMax = minOf(bodyH.dp, cardHMax - 80.dp)

            Row(
                Modifier
                    .fillMaxWidth()
                    .height(bodyMax)
                    .padding(top = 8.dp)
                    .devBox()
            ) {

                Column(
                    Modifier
                        .width(96.dp)

                        .fillMaxHeight()
                        .clip(RoundedCornerShape(12.dp))
                        .background(SidebarBg)
                        .padding(4.dp)
                        .devBox()
                ) {
                    cats.forEachIndexed { i, name ->
                        val bmp = remember(i) {
                            CategoryIcons.get(activity, GamePaths.sourceDir, name, i)
                        }
                        Row(
                            Modifier
                                .fillMaxWidth()

                                .weight(1f)
                                .clip(RoundedCornerShape(9.dp))
                                .background(if (name == selected) Accent else Color.Transparent)
                                .clickable { onSelect(name) }
                                .padding(horizontal = 6.dp, vertical = 8.dp)
                                .devBox(),
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

                Column(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .clip(RoundedCornerShape(12.dp))
                        .background(CardBg)
                        .padding(10.dp)
                        .devBox()
                ) {
                    when (selected) {
                        "材质替换" -> Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                            MaterialTab(activity)
                        }
                        "音乐" -> AndroidView({ MusicPlayer.buildView(activity) }, Modifier.fillMaxSize())
                        "脚本" -> AndroidView({ PyLoaderUi.buildView(activity) }, Modifier.fillMaxSize())
                        "快捷键" -> {
                            val cfg = hkCfg
                            if (cfg != null) {

                                var cfgGen by remember(cfg) { mutableIntStateOf(0) }
                                LaunchedEffect(cfg) {
                                    McpBridge.refreshConfig(activity, cfg.name)
                                    kotlinx.coroutines.delay(350)
                                    cfgGen++
                                }
                                Column(Modifier.fillMaxSize()) {

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

                        "扩展" -> {
                            val st = rememberScrollState(PanelScroll.ext)
                            LaunchedEffect(st.value) { PanelScroll.ext = st.value }
                            Column(Modifier.fillMaxSize().verticalScroll(st)) {
                                ExtensionTab(activity)
                            }
                        }

                        "设置" -> {
                            val st = rememberScrollState(PanelScroll.settings)
                            LaunchedEffect(st.value) { PanelScroll.settings = st.value }
                            Column(Modifier.fillMaxSize().verticalScroll(st)) {
                                SettingsScreen(
                                    activity,
                                    onOpenCookie = { PanelScroll.cookiePage = true }
                                )
                            }
                        }
                    }
                }
            }
            }
        }
    }
}

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

        if (DevMode.enabled) {
            var choice by remember { mutableIntStateOf(ResourceMount.mode(activity)) }
            Row(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                modifier = Modifier.fillMaxWidth(),
            ) {
                listOf(
                    ResourceMount.MODE_OFFICIAL to "官方包",
                    ResourceMount.MODE_MIRROR to "镜像目录",
                ).forEach { (value, label) ->
                    Box(
                        Modifier
                            .weight(1f)
                            .clip(RoundedCornerShape(8.dp))
                            .background(
                                if (choice == value) Color(0x660A84FF) else Color(0x22FFFFFF)
                            )
                            .clickable {
                                choice = value
                                ResourceMount.setMode(activity, value)
                                toast("注入方式：$label")
                            }
                            .padding(vertical = 7.dp)
                            .devBox(),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(label, fontSize = 11.sp, color = Color.White)
                    }
                }
            }
        }
        actions.forEach { (label, run) ->
            Box(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp))
                    .background(Color(0x33FFFFFF))
                    .clickable {

                        toast("$label …")
                        Thread({
                            val msg = runCatching { run() }.getOrElse { it.message ?: "失败" }
                            toast(msg)
                        }, "texinject-pack").start()
                    }
                    .padding(vertical = 12.dp)
                    .devBox(),
                contentAlignment = Alignment.Center
            ) {
                Text(label, fontSize = 13.sp, color = Color.White)
            }
        }
    }
}
