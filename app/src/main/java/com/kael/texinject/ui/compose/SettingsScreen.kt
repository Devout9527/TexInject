package com.kael.texinject.ui.compose

import android.app.Activity
import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.focus.onFocusChanged
import com.kael.texinject.ui.OverlayManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.kael.texinject.ui.Brand
import com.kael.texinject.MusicPlayer
import com.kael.texinject.UpdateUi
import com.kael.texinject.Updater
import com.kael.texinject.ui.AntiRecord
import com.kael.texinject.ui.NowPlayingBar
import com.kael.texinject.ui.WebsiteFloatingWindow

private val Accent = Color(0xFF0A84FF)
private val Muted = Color(0xFF8899A6)
private val Field = Color(0x33FFFFFF)

@Composable
private fun SectionTitle(text: String) {
    Text(
        text = text,
        fontSize = 13.sp,
        fontWeight = FontWeight.Bold,
        color = Color(0xFFA9A9A9),
        modifier = Modifier.padding(top = 8.dp, bottom = 6.dp).devBox()
    )
}

@Composable
private fun Hint(text: String) {
    Text(
        text, fontSize = 11.sp, color = Muted,
        modifier = Modifier.padding(top = 6.dp).devBox()
    )
}

@Composable
private fun GlassField(value: String, hint: String, onValueChange: (String) -> Unit) {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    androidx.compose.foundation.layout.Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(Field)
            .clickable {
                val act = generateSequence(ctx) { (it as? android.content.ContextWrapper)?.baseContext }
                    .filterIsInstance<android.app.Activity>().firstOrNull()
                if (act != null) {
                    com.kael.texinject.ui.InputDialog.show(act, hint, value, onValueChange)
                }
            }
            .padding(horizontal = 12.dp, vertical = 10.dp)
            .devBox(),
        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically
    ) {
        Text(
            if (value.isEmpty()) hint else value,
            fontSize = 13.sp,
            color = if (value.isEmpty()) Color(0xFF66707A) else Color.White,
            maxLines = 1,
            overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f)
        )
        Text("›", fontSize = 15.sp, color = Color(0xFF8E8E93))
    }
}

@Composable
private fun AccentButton(text: String, onClick: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .padding(top = 6.dp)
            .background(Accent.copy(alpha = 0.8f), RoundedCornerShape(10.dp))
            .clickable { onClick() }
            .padding(vertical = 10.dp)
            .devBox(),
        contentAlignment = Alignment.Center
    ) {
        Text(text, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = Color.White)
    }
}

@Composable
private fun LabeledSlider(
    label: String,
    min: Int,
    max: Int,
    value: Int,
    display: (Int) -> String,
    onChange: (Int) -> Unit
) {
    var v by remember { mutableIntStateOf(value) }
    Text("$label：${display(v)}", fontSize = 12.sp, color = Color.White)
    Slider(
        value = v.toFloat(),
        onValueChange = { f ->
            val n = f.toInt().coerceIn(min, max)
            v = n
            onChange(n)
        },
        valueRange = min.toFloat()..max.toFloat(),
        modifier = Modifier.fillMaxWidth().devBox()
    )
}

@Composable
fun SettingsScreen(activity: Activity, onOpenCookie: () -> Unit = {}) {
    val ctx: Context = activity

    Column(Modifier.fillMaxWidth()) {

        SectionTitle("音乐播放模式")
        var mode by remember { mutableIntStateOf(MusicPlayer.getPlayMode(activity)) }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf("顺序", "随机", "单曲循环").forEachIndexed { i, label ->
                Box(
                    Modifier
                        .weight(1f)
                        .height(40.dp)
                        .background(
                            if (i == mode) Accent else Color(0x59FFFFFF),
                            RoundedCornerShape(9.dp)
                        )
                        .clickable {
                            MusicPlayer.setPlayMode(activity, i)
                            mode = i
                        }
                        .devBox(),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        label,
                        fontSize = 13.sp,
                        color = if (i == mode) Color.White else Color(0xFF141414)
                    )
                }
            }
        }
        Hint("顺序播放 / 随机播放 / 单曲循环。")

        SectionTitle("悬浮播放条尺寸")
        LabeledSlider(
            "宽度", NowPlayingBar.MIN_W, NowPlayingBar.MAX_W,
            NowPlayingBar.getWidthDp(activity), { "${it}dp" }
        ) { w -> NowPlayingBar.setSize(activity, w, NowPlayingBar.getHeightDp(activity)) }
        LabeledSlider(
            "高度", NowPlayingBar.MIN_H, NowPlayingBar.MAX_H,
            NowPlayingBar.getHeightDp(activity), { "${it}dp" }
        ) { h -> NowPlayingBar.setSize(activity, NowPlayingBar.getWidthDp(activity), h) }
        Hint("拖动即时生效并记住。文字会随尺寸自动跑马灯滚动。")

        SectionTitle("网易云登录")
        Hint("在线播放 / VIP 歌曲需要登录。登录状态用 AES-256 加密存在应用私有目录。")
        AccentButton("扫码登录 / 手机号登录") { MusicPlayer.showLoginDialog(activity) }

        SectionTitle("检查更新")
        Hint("当前版本：${Updater.currentVersion(activity)}\n来源：github.com/${Updater.REPO}")
        AccentButton("立即检查更新") { UpdateUi.checkAndShow(activity, false) }

        SectionTitle("主界面文字")
        var quote by remember { mutableStateOf(Brand.quote(activity)) }
        GlassField(quote, Brand.DEFAULT_QUOTE) {
            quote = it
            Brand.setQuote(activity, it)
        }
        Hint("显示在面板标题「TexInject」后面，留空则只显示 TexInject。")

        SectionTitle("打开自定义网站")
        val webPrefs = activity.getSharedPreferences("texinject_website", Context.MODE_PRIVATE)
        var url by remember { mutableStateOf(webPrefs.getString("url", "") ?: "") }
        GlassField(url, "输入网址，例如 www.example.com") {
            url = it
            webPrefs.edit().putString("url", it).apply()
        }
        AccentButton("打开") { WebsiteFloatingWindow.show(activity, url.trim()) }
        LabeledSlider(
            "窗口宽度", 20, 100, WebsiteFloatingWindow.widthPercent(activity).toInt(), { "${it}%" }
        ) { WebsiteFloatingWindow.setWidthPercent(activity, it.toFloat()) }
        LabeledSlider(
            "窗口高度", 20, 100, WebsiteFloatingWindow.heightPercent(activity).toInt(), { "${it}%" }
        ) { WebsiteFloatingWindow.setHeightPercent(activity, it.toFloat()) }

        SectionTitle("面板尺寸")
        var pw by remember { mutableIntStateOf(PanelSize.widthDp(activity)) }
        LabeledSlider(
            "左右宽度", PanelSize.W_MIN, PanelSize.W_MAX, pw, { "${it}dp" }
        ) { pw = it; PanelSize.setWidthDp(activity, it) }
        var ph by remember { mutableIntStateOf(PanelSize.heightDp(activity)) }
        LabeledSlider(
            "上下高度", PanelSize.H_MIN, PanelSize.H_MAX, ph, { "${it}dp" }
        ) { ph = it; PanelSize.setHeightDp(activity, it) }
        Hint("宽度最多 94% 屏宽、高度最多 91% 屏高，调大也不会超出屏幕。拖动即时生效。")

        SectionTitle("Cookie 登录")
        AccentButton("打开 Cookie 登录") { onOpenCookie() }

        SectionTitle("防录屏")
        var anti by remember { mutableStateOf(AntiRecord.enabled(activity)) }
        Row(
            Modifier.fillMaxWidth().padding(vertical = 6.dp).devBox(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "截屏 / 录屏时隐藏模块界面",
                fontSize = 13.sp,
                color = Color.White,
                modifier = Modifier.weight(1f)
            )
            Switch(checked = anti, onCheckedChange = {
                anti = it
                AntiRecord.setEnabled(activity, it)
            })
        }

        SectionTitle("模块说明")
        Text(
            "TexInject · Minecraft 中国版（网易版）增强面板\n" +
                "适配游戏版本：3.9.15.297907\n" +
                "框架：LSPosed（现代 LibXposed API 102） · 仅 arm64-v8a",
            fontSize = 12.sp, color = Color.White
        )
    }
}
