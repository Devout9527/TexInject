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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.kael.texinject.CookiePrefs
import com.kael.texinject.CookieStore
import com.kael.texinject.SauthProbe
import com.kael.texinject.ui.InputDialog
import kotlinx.coroutines.delay

private val CAccent = Color(0xFF0A84FF)
private val CMuted = Color(0xFF8899A6)
private val CCard = Color(0x1FFFFFFF)

@Composable
private fun CookieSection(text: String) {
    Text(
        text = text,
        fontSize = 13.sp,
        fontWeight = FontWeight.Bold,
        color = Color(0xFFA9A9A9),
        modifier = Modifier.padding(top = 10.dp, bottom = 6.dp).devBox()
    )
}

@Composable
private fun CookieHint(text: String) {
    Text(text, fontSize = 11.sp, color = CMuted, modifier = Modifier.padding(top = 6.dp).devBox())
}

@Composable
private fun CookieButton(text: String, onClick: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .padding(top = 6.dp)
            .background(CAccent.copy(alpha = 0.8f), RoundedCornerShape(10.dp))
            .clickable { onClick() }
            .padding(vertical = 10.dp)
            .devBox(),
        contentAlignment = Alignment.Center
    ) {
        Text(text, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = Color.White)
    }
}

private fun toast(activity: Activity, msg: String) {
    try {
        Toast.makeText(activity, msg, Toast.LENGTH_SHORT).show()
    } catch (_: Throwable) {
    }
}

private inline fun safe(activity: Activity, block: () -> Unit) {
    try {
        block()
    } catch (t: Throwable) {
        toast(activity, "操作失败：" + (t.message ?: t.javaClass.simpleName))
    }
}

@Composable
fun CookieScreen(activity: Activity, onBack: () -> Unit) {
    val ctx: Context = activity

    var enabled by remember { mutableStateOf(CookiePrefs.enabled(ctx)) }
    var auto by remember { mutableStateOf(CookiePrefs.autoLogin(ctx)) }
    var selected by remember { mutableStateOf(CookiePrefs.selected(ctx)) }
    var files by remember { mutableStateOf(CookieStore.list(ctx)) }
    var armed by remember { mutableStateOf(SauthProbe.armCapture) }

    LaunchedEffect(Unit) {
        while (true) {
            try {
                files = CookieStore.list(ctx)
                armed = SauthProbe.armCapture
            } catch (_: Throwable) {
            }
            delay(1500)
        }
    }

    Column(Modifier.fillMaxWidth()) {

        Row(
            Modifier.fillMaxWidth().padding(bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "‹ 返回",
                fontSize = 14.sp,
                color = CAccent,
                modifier = Modifier
                    .clickable { onBack() }
                    .padding(vertical = 6.dp, horizontal = 2.dp)
            )
        }

        Text("Cookie 登录", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White)
        CookieHint(
            (if (selected.isEmpty()) "未选择" else CookieStore.display(selected)) + " · " + when {
                !enabled -> "未启用"
                !auto -> "自动登录关闭"
                selected.isEmpty() -> "未选择 cookie"
                SauthProbe.hookedClass -> "已注入"
                else -> "等待 UniSDK"
            }
        )

        CookieSection("开关")
        Row(
            Modifier.fillMaxWidth().padding(vertical = 4.dp).devBox(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("启用 Cookie 登录", fontSize = 13.sp, color = Color.White, modifier = Modifier.weight(1f))
            Switch(checked = enabled, onCheckedChange = {
                enabled = it
                CookiePrefs.setEnabled(ctx, it)
                toast(activity, if (it) "已开启" else "已关闭")
            })
        }
        Row(
            Modifier.fillMaxWidth().padding(vertical = 4.dp).devBox(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("自动登录", fontSize = 13.sp, color = Color.White, modifier = Modifier.weight(1f))
            Switch(checked = auto, onCheckedChange = {
                auto = it
                CookiePrefs.setAutoLogin(ctx, it)
            })
        }

        CookieSection("捕获")
        CookieButton(if (armed) "等待下一次登录…（点击取消）" else "捕获下一次登录") {
            safe(activity) {
                SauthProbe.armCapture = !SauthProbe.armCapture
                armed = SauthProbe.armCapture
                toast(activity, if (armed) "登录后按账号 ID 存成 <ID>.txt" else "已取消捕获")
            }
        }

        CookieSection("已保存（${files.size}）")
        if (files.isEmpty()) {
            Text("还没有 cookie。先点上面的「捕获下一次登录」。", fontSize = 12.sp, color = CMuted)
        }
        if (files.size > 100) {
            CookieHint("只显示最新 100 条（共 ${files.size} 条）。下面是「清空全部」。")
        }
        files.take(100).forEach { name ->
            val isSel = name == selected
            val preview = if (isSel) {
                try {
                    CookieStore.read(ctx, name)?.take(80)?.replace("\n", " ") ?: ""
                } catch (t: Throwable) {
                    ""
                }
            } else {
                ""
            }
            Row(
                Modifier
                    .fillMaxWidth()
                    .padding(vertical = 3.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (isSel) CAccent.copy(alpha = 0.30f) else CCard)
                    .clickable {
                        safe(activity) {
                            selected = name
                            CookiePrefs.setSelected(ctx, name)
                            toast(activity, "已选中：" + CookieStore.display(name))
                        }
                    }
                    .padding(horizontal = 10.dp, vertical = 8.dp)
                    .devBox(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(Modifier.weight(1f)) {
                    Text(
                        (if (isSel) "● " else "") + CookieStore.display(name),
                        fontSize = 13.sp,
                        color = Color.White,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        if (isSel) preview else "点击选中后可回填",
                        fontSize = 10.sp,
                        color = CMuted,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                Text(
                    "改名",
                    fontSize = 12.sp,
                    color = CAccent,
                    modifier = Modifier
                        .clickable {
                            safe(activity) {
                                InputDialog.show(activity, "Cookie 名称", CookieStore.display(name)) { newName ->
                                    safe(activity) {
                                        val n = CookieStore.sanitize(CookieStore.withExt(newName))
                                        if (n.isNotEmpty() && n != name) {
                                            if (CookieStore.rename(ctx, name, n)) {
                                                if (selected == name) {
                                                    selected = n
                                                    CookiePrefs.setSelected(ctx, n)
                                                }
                                                files = CookieStore.list(ctx)
                                                toast(activity, "已改名：$n")
                                            } else {
                                                toast(activity, "改名失败（重名？）")
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        .padding(horizontal = 6.dp, vertical = 4.dp)
                )
                Text(
                    "导出",
                    fontSize = 12.sp,
                    color = CAccent,
                    modifier = Modifier
                        .clickable {
                            safe(activity) {
                                val p = CookieStore.exportOne(ctx, name)
                                toast(activity, if (p != null) "已导出：$p" else "导出失败")
                            }
                        }
                        .padding(horizontal = 6.dp, vertical = 4.dp)
                )
                Text(
                    "删除",
                    fontSize = 12.sp,
                    color = Color(0xFFFF6B6B),
                    modifier = Modifier
                        .clickable {
                            safe(activity) {
                                val ok = CookieStore.delete(ctx, name)
                                if (selected == name) {
                                    selected = ""
                                    CookiePrefs.setSelected(ctx, "")
                                }
                                files = CookieStore.list(ctx)
                                toast(activity, if (ok) "已删除：$name" else "删除失败")
                            }
                        }
                        .padding(horizontal = 6.dp, vertical = 4.dp)
                )
            }
        }

        CookieSection("导入 / 导出")
        CookieButton("导入（粘贴 cookie）") {
            safe(activity) {
                InputDialog.showPair(
                    activity,
                    "导入 Cookie",
                    "名字",
                    "",
                    "内容",
                    ""
                ) { nm, data ->
                    safe(activity) {
                        if (data.isBlank()) {
                            toast(activity, "内容为空")
                            return@safe
                        }
                        val base = nm.trim()
                            .ifEmpty { CookieStore.nameFromSauth(data) }
                            .ifEmpty { "导入-" + System.currentTimeMillis() % 100000 }
                        val name = CookieStore.sanitize(CookieStore.withExt(base))
                        CookieStore.write(ctx, name, data.trim())
                        files = CookieStore.list(ctx)
                        toast(activity, "已导入：" + CookieStore.display(name))
                    }
                }
            }
        }
        CookieButton("导出全部（cookies_export）") {
            safe(activity) {
                var n = 0
                for (f in files) {
                    if (CookieStore.exportOne(ctx, f) != null) n++
                }
                toast(activity, if (n > 0) "已导出 $n 条" else "没有可导出的 cookie")
            }
        }
        CookieHint(CookieStore.dir(ctx).absolutePath)

        Box(Modifier.height(12.dp))
    }
}
