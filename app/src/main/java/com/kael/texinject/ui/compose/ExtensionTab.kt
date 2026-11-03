/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 「扩展」页：只留 Cookie 登录和相机穿墙。
 *
 * 别的（NBT 编辑器、设置耐久、杀戮光环、诊断…）实测要么没效果要么掉帧，
 * 已经从 UI 上撤掉 —— 留在那儿只会让人以为坏了。
 */
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
import androidx.compose.runtime.mutableIntStateOf
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
    var page by remember { mutableIntStateOf(0) }

    // 快捷键按下只摆出悬浮开关，点开关才开窗口
    remember {
        ExtHotkeys.register(
            ExtHotkeys.Action("ext_cookie", "Cookie 登录") { act, on ->
                if (on) FloatingHost.show(act, "Cookie 登录") { CookieScreen(act) {} }
                else FloatingHost.close()
            }
        )
        true
    }

    when (page) {
        1 -> CookieScreen(activity) { page = 0 }
        else -> Column(
            Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            var cookieOn by remember {
                mutableStateOf(ExtHotkeys.isEnabled(activity, "ext_cookie"))
            }
            Entry(
                "Cookie 登录",
                "多账号免密切换，一个文件一条 cookie",
                cookieOn,
                { page = 1 },
                {
                    val want = !cookieOn
                    ExtHotkeys.setEnabled(activity, "ext_cookie", want)
                    cookieOn = want
                },
            ) {
                ExtHotkeys.showWidget(activity, "ext_cookie")
            }

            // 下载捕获：游戏下载资源时直接写到外部目录，不用事后再复制。
                    // 走 native 的 PLT/GOT hook，把 storge/stream 的读写整体换路径。
                    // 用 SP 记忆：一开就真开（native 只能开不能关），重进面板从 SP 恢复"开"，
                    // 不会自己变回关。SP 里记的才是真实状态。
                    val mirrorSp = activity.getSharedPreferences("texinject_ext_feature", Context.MODE_PRIVATE)
                    var mirror by remember { mutableStateOf(mirrorSp.getBoolean("mirror_stream", false)) }
                    ToggleRow(
                        "下载捕获",
                        "游戏下载的资源直接落到外面，下次下载才生效",
                        mirror
                    ) {
                        if (mirror) {
                            Toast.makeText(activity, "这个开关只能在重启游戏后关掉", Toast.LENGTH_SHORT).show()
                            return@ToggleRow
                        }
                        val root = java.io.File(
                            activity.getExternalFilesDir(null), "pack_netease/Download"
                        ).absolutePath
                        val ok = runCatching {
                            com.kael.texinject.nativecore.NativeCore.ioTraceMirror(root)
                        }.getOrDefault(false)
                        if (ok) {
                            mirror = true
                            mirrorSp.edit().putBoolean("mirror_stream", true).apply()
                        }
                        Toast.makeText(
                            activity,
                            if (ok) "已开：下载内容会直接写到\n$root" else "装不上（so 还没加载？）",
                            Toast.LENGTH_LONG
                        ).show()
                    }

                    // 相机穿墙：native 能开能关，初始状态从 SP 恢复（上次怎么设的就怎么显示）
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

/**
 * 扩展条目：标题 + 说明 + 功能总开关 + 悬浮按钮。
 *
 * 两个控件是分开的：开关关掉时快捷键不响应、点「悬浮」也不弹；
 * 「悬浮」只负责把窗口摆到屏幕上。
 */
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
        // 功能总开关
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
        // 浮出去：独立窗口，一边看游戏一边用
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

/** 一行开关：标题 + 说明 + 右侧胶囊。 */
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

/** 一行操作：标题 + 说明 + 右侧按钮。 */
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
