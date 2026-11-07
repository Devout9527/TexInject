package com.kael.texinject.ui.compose

import android.app.Activity
import android.content.Context
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.kael.texinject.ExtHotkeys
import com.kael.texinject.nativecore.Game
import com.kael.texinject.ui.FloatingHost

private val Accent2 = Color(0xFF0062FF)
private val RowBg = Color(0x1AFFFFFF)
private val Muted2 = Color(0xFF8E8E93)

@Composable
fun ExtensionTab(activity: Activity) {

    var page by remember { mutableStateOf("") }

    remember {
        ExtHotkeys.register(
            ExtHotkeys.Action("ext_skin", "皮肤替换") { act, on ->
                if (on) FloatingHost.show(act, "皮肤替换") { SkinScreen(act) {} }
                else FloatingHost.close()
            }
        )
        true
    }

    when (page) {
        "skin" -> SkinScreen(activity) { page = "" }
        else -> Column(
            Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {

            var skinOn by remember {
                mutableStateOf(ExtHotkeys.isEnabled(activity, "ext_skin"))
            }
            Entry(
                "皮肤替换",
                "穿着 A 的名字，显示 B 的样子（先备份再覆盖，可还原）",
                skinOn,
                { page = "skin" },
                {
                    val want = !skinOn
                    ExtHotkeys.setEnabled(activity, "ext_skin", want)
                    skinOn = want
                },
            ) {
                ExtHotkeys.showWidget(activity, "ext_skin")
            }

            val mirrorSp = activity.getSharedPreferences("texinject_ext_feature", Context.MODE_PRIVATE)

            if (mirrorSp.contains("mirror_stream")) {
                mirrorSp.edit().remove("mirror_stream").apply()
            }
            var mirror by remember {
                mutableStateOf(
                    runCatching {
                        com.kael.texinject.nativecore.NativeCore.ioTraceMirrorOn()
                    }.getOrDefault(false)
                )
            }
            ToggleRow(
                "下载捕获",
                "游戏下载的资源直接落到外面，下次下载才生效",
                mirror
            ) {

                val now = runCatching {
                    com.kael.texinject.nativecore.NativeCore.ioTraceMirrorOn()
                }.getOrDefault(false)
                if (now) {
                    Toast.makeText(activity, "这个开关只能在重启游戏后关掉", Toast.LENGTH_SHORT).show()
                    mirror = true
                    return@ToggleRow
                }
                val root = java.io.File(
                    activity.getExternalFilesDir(null), "pack_netease/Download"
                ).absolutePath
                val ok = runCatching {
                    com.kael.texinject.nativecore.NativeCore.ioTraceMirror(root)
                }.getOrDefault(false)

                mirror = runCatching {
                    com.kael.texinject.nativecore.NativeCore.ioTraceMirrorOn()
                }.getOrDefault(false)
                com.kael.texinject.log.TxLog.feature(
                    "下载捕获", mirror, if (mirror) "落点 $root" else "装不上"
                )
                Toast.makeText(
                    activity,
                    if (mirror) "已开：下载内容会直接写到\n$root"
                    else if (ok) "装了 hook，但状态没起来（看 logs 目录）"
                    else "装不上（so 还没加载？）",
                    Toast.LENGTH_LONG
                ).show()
            }

            var camera by remember { mutableStateOf(mirrorSp.getBoolean("camera_clip", false)) }
            ToggleRow(
                "相机穿墙",
                "镜头不再被方块顶回来（hook 相机避让，实验功能）",
                camera
            ) {
                val want = !camera
                val ok = Game.setFeature(Game.CAMERA_CLIP, want)
                if (ok) {
                    camera = want
                    mirrorSp.edit().putBoolean("camera_clip", want).apply()
                }
                com.kael.texinject.log.TxLog.feature(
                    "相机穿墙", ok && want, if (ok) "" else "装不上"
                )
                Toast.makeText(
                    activity,
                    when {
                        !ok -> "这个版本装不上（看 logcat 的 TexInjectGame）"
                        want -> "相机穿墙已开"
                        else -> "相机穿墙已关"
                    },
                    Toast.LENGTH_SHORT
                ).show()
            }
        }
    }
}

@Composable
private fun Entry(
    title: String,
    hint: String,
    enabled: Boolean,
    onClick: () -> Unit,
    onToggle: () -> Unit,
    onFloat: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .background(RowBg, RoundedCornerShape(10.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(
            Modifier.weight(1f).clickable { onClick() }
        ) {
            Text(title, fontSize = 13.sp, color = Color.White, fontWeight = FontWeight.Bold)
            Text(hint, fontSize = 10.sp, color = Muted2, modifier = Modifier.padding(top = 2.dp))
        }

        Box(
            Modifier
                .size(34.dp, 19.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(if (enabled) Color(0xFF00C853) else Color(0x33FFFFFF))
                .clickable { onToggle() }
        ) {
            Box(
                Modifier
                    .align(if (enabled) Alignment.CenterEnd else Alignment.CenterStart)
                    .padding(2.dp)
                    .size(15.dp)
                    .clip(CircleShape)
                    .background(Color.White)
            )
        }

        Box(
            Modifier
                .padding(start = 6.dp)
                .background(
                    if (enabled) Color(0x330A84FF) else Color(0x14FFFFFF),
                    RoundedCornerShape(8.dp)
                )
                .clickable { onFloat() }
                .padding(horizontal = 8.dp, vertical = 6.dp)
        ) {
            Text(
                "悬浮",
                fontSize = 10.sp,
                color = if (enabled) Color(0xFF7FB2FF) else Color(0x55FFFFFF)
            )
        }
    }
}

@Composable
private fun ToggleRow(title: String, hint: String, on: Boolean, onToggle: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .background(RowBg, RoundedCornerShape(10.dp))
            .clickable { onToggle() }
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, fontSize = 13.sp, color = Color.White, fontWeight = FontWeight.Bold)
            Text(hint, fontSize = 10.sp, color = Muted2, modifier = Modifier.padding(top = 2.dp))
        }
        Box(
            Modifier
                .size(40.dp, 22.dp)
                .clip(RoundedCornerShape(11.dp))
                .background(if (on) Accent2 else Color(0x33FFFFFF))
        ) {
            Box(
                Modifier
                    .align(if (on) Alignment.CenterEnd else Alignment.CenterStart)
                    .padding(2.dp)
                    .size(18.dp)
                    .clip(CircleShape)
                    .background(Color.White)
            )
        }
    }
}

@Composable
private fun ActionRow(title: String, hint: String, action: String, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .background(RowBg, RoundedCornerShape(10.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, fontSize = 13.sp, color = Color.White, fontWeight = FontWeight.Bold)
            Text(hint, fontSize = 10.sp, color = Muted2, modifier = Modifier.padding(top = 2.dp))
        }
        Box(
            Modifier
                .background(Color(0x330A84FF), RoundedCornerShape(8.dp))
                .clickable { onClick() }
                .padding(horizontal = 10.dp, vertical = 6.dp)
        ) {
            Text(action, fontSize = 11.sp, color = Color(0xFF7FB2FF))
        }
    }
}
