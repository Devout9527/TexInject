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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.kael.texinject.SkinStore
import com.kael.texinject.log.TxLog

private val SAccent = Color(0xFF0A84FF)
private val SMuted = Color(0xFF8899A6)
private val SCard = Color(0x1FFFFFFF)
private val SGreen = Color(0xFF30D158)

@Composable
fun SkinScreen(activity: Activity, onBack: () -> Unit) {
    val ctx: Context = activity

    var skins by remember { mutableStateOf(SkinStore.list(ctx)) }
    var uids by remember { mutableStateOf(SkinStore.uids(ctx)) }

    var uid by remember { mutableStateOf(SkinStore.recentUid(ctx)) }

    fun autoSlot(): String {
        val fromUid = SkinStore.skinOf(ctx, uid)
        if (skins.any { it.id == fromUid }) return fromUid

        val recent = SkinStore.recentIds(ctx)
        return skins.firstOrNull { it.id in recent }?.id ?: skins.firstOrNull()?.id.orEmpty()
    }

    var target by remember { mutableStateOf(autoSlot()) }

    var slotManual by remember { mutableStateOf(false) }
    var source by remember {
        mutableStateOf(skins.firstOrNull { it.id != target && it.usable }?.id.orEmpty())
    }
    var busy by remember { mutableStateOf(false) }

    var slotOpen by remember { mutableStateOf(false) }

    var warnShown by remember {
        mutableStateOf(activity.getSharedPreferences("texinject_skin", Context.MODE_PRIVATE)
            .getBoolean("risk_ack", false))
    }
    var showWarn by remember { mutableStateOf(false) }

    fun toast(msg: String) = Toast.makeText(activity, msg, Toast.LENGTH_SHORT).show()

    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(6.dp)) {

        Row(
            Modifier.fillMaxWidth().padding(bottom = 2.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "‹ 返回",
                fontSize = 14.sp,
                color = SAccent,
                modifier = Modifier.clickable { onBack() }.padding(vertical = 6.dp, horizontal = 2.dp)
            )
        }

        Text("皮肤替换", fontSize = 18.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Text(
            "把「外观」的包体覆盖到「槽位」上：皮肤名还是槽位的，样子变成外观的。",
            fontSize = 11.sp, color = SMuted, modifier = Modifier.padding(top = 2.dp)
        )

        SectionLabel("账号（UID）")
        if (uids.isEmpty()) {
            Text(
                "users/ 下没有账号目录，先进游戏登录一次。",
                fontSize = 12.sp, color = SMuted
            )
        }
        uids.forEach { u ->
            val cur = SkinStore.skinOf(ctx, u)
            val curName = skins.firstOrNull { it.id == cur }?.name
            PickerRow(
                title = u,
                tag = if (u == uid) "当前" else "",
                tagColor = SGreen,
                subtitle = if (curName != null) "正在使用 $curName" else "没有 skin.txt 记录",
                selected = u == uid,
                dim = false,
            ) {
                uid = u

                if (!slotManual) {
                    target = autoSlot()
                    if (source == target) {
                        source = skins.firstOrNull { it.id != target && it.usable }?.id.orEmpty()
                    }
                }
            }
        }

        SectionLabel("要修改的皮肤（槽位）")
        val tSel = skins.firstOrNull { it.id == target }
        PickerRow(
            title = tSel?.name ?: "未选择",
            tag = tSel?.kind ?: "",
            tagColor = if (tSel?.is4D == true) SGreen else SAccent,
            subtitle = if (tSel != null) subtitleOf(tSel) else "点这里选择要改的皮肤",
            selected = true,
            dim = false,
            trailing = if (slotOpen) "收起" else "更换",
        ) {
            slotOpen = !slotOpen
            if (slotOpen && !warnShown) {

                showWarn = true
                warnShown = true
                activity.getSharedPreferences("texinject_skin", Context.MODE_PRIVATE)
                    .edit().putBoolean("risk_ack", true).apply()
            }
        }

        if (slotOpen) {
            if (slotManual) {
                Text(
                    "恢复为账号自动选择",
                    fontSize = 11.sp, color = SAccent,
                    modifier = Modifier.clickable {
                        slotManual = false
                        target = autoSlot()
                        slotOpen = false
                    }.padding(vertical = 3.dp)
                )
            }
            if (skins.isEmpty()) {
                Text(
                    "skin_records 目录是空的，先进游戏看一次皮肤再回来。",
                    fontSize = 12.sp, color = SMuted
                )
            }
            skins.forEach { sk ->
                PickerRow(
                    title = sk.name,
                    tag = sk.kind,
                    tagColor = if (sk.is4D) SGreen else SAccent,
                    subtitle = subtitleOf(sk),
                    selected = sk.id == target,
                    dim = !sk.usable,
                ) {
                    target = sk.id
                    slotManual = true
                    slotOpen = false
                }
            }
        }

        SectionLabel("注入外观（来源）")
        skins.forEach { s ->
            PickerRow(
                title = s.name,
                tag = s.kind,
                tagColor = if (s.is4D) SGreen else SAccent,
                subtitle = subtitleOf(s),
                selected = s.id == source,
                dim = !s.usable,
            ) {
                source = s.id
            }
        }

        SectionLabel("操作")
        val t = skins.firstOrNull { it.id == target }
        val s = skins.firstOrNull { it.id == source }
        val tn = t?.name ?: "—"
        val sn = s?.name ?: "—"
        val crossKind = t != null && s != null && t.is4D != s.is4D
        Text(
            "将用「$sn」(${s?.kind ?: "—"}) 覆盖「$tn」(${t?.kind ?: "—"})",
            fontSize = 12.sp, color = Color.White, modifier = Modifier.padding(vertical = 2.dp)
        )
        if (crossKind) {
            Text(
                "跨类型：会连记录一起改（is_pack / second_type / path），" +
                    "原 png 或 zip 留在原处不动，还原时会连记录一起还原。",
                fontSize = 10.sp, color = Color(0xFFFFB74D),
                modifier = Modifier.padding(bottom = 2.dp)
            )
        }
        BigButton(
            text = if (busy) "处理中…" else "备份并替换",
            enabled = !busy && target.isNotEmpty() &&
                source.isNotEmpty() && target != source
        ) {
            busy = true

            Thread({
                val r = runCatching { SkinStore.swap(ctx, target, source) }
                    .getOrElse { SkinStore.Result(false, it.message ?: "失败") }
                TxLog.i(
                    "Skin",
                    (if (r.ok) "替换成功 " else "替换失败 ") + "$target <- $source：" + r.summary
                )
                activity.runOnUiThread {
                    busy = false
                    skins = SkinStore.list(ctx)
                    toast(r.summary)
                }
            }, "texinject-skin").start()
        }

        BigButton(
            text = "还原这个槽位",
            enabled = !busy && target.isNotEmpty() &&
                (skins.firstOrNull { it.id == target }?.hasBackup == true),
            color = Color(0x33FFFFFF)
        ) {
            busy = true
            Thread({
                val r = SkinStore.restore(ctx, target)
                TxLog.i("Skin", "还原 $target：" + r.summary)
                activity.runOnUiThread {
                    busy = false
                    skins = SkinStore.list(ctx)
                    toast(r.summary)
                }
            }, "texinject-skin").start()
        }

        BigButton("全部还原", enabled = !busy, color = Color(0x33FFFFFF)) {
            busy = true
            Thread({
                val r = SkinStore.restoreAll(ctx)
                TxLog.i("Skin", "全部还原：" + r.summary)
                activity.runOnUiThread {
                    busy = false
                    skins = SkinStore.list(ctx)
                    toast(r.summary)
                }
            }, "texinject-skin").start()
        }

        if (showWarn) WarnCard(onConfirm = { showWarn = false })

        Text(
            "备份在 files/TexInject/Backup/skin/，覆盖前先备份，失败会整个放弃。" +
                "游戏里换一下皮肤或重启才能看到变化。",
            fontSize = 10.sp, color = SMuted, modifier = Modifier.padding(top = 4.dp, bottom = 8.dp)
        )
    }
}

@Composable
private fun WarnCard(onConfirm: () -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .background(Color(0x33FF6B4A), RoundedCornerShape(10.dp))
            .padding(horizontal = 10.dp, vertical = 9.dp)
    ) {
        Text(
            "注意",
            fontSize = 13.sp, fontWeight = FontWeight.Bold, color = Color(0xFFFFB4A0)
        )
        Text(
            "皮肤修改会导致无法进入世界。如果改过之后进不去世界，" +
                "请用这里的「还原这个槽位」或「全部还原」退回去。",
            fontSize = 11.sp, color = Color(0xFFFFD9CE),
            modifier = Modifier.padding(top = 3.dp)
        )
        Text(
            "照片改 4D：改完必须重启游戏才生效。",
            fontSize = 11.sp, color = Color(0xFFFFD9CE),
            modifier = Modifier.padding(top = 3.dp)
        )
        Row(
            Modifier.fillMaxWidth().padding(top = 8.dp),
            horizontalArrangement = Arrangement.End
        ) {
            Box(
                Modifier
                    .background(Color(0x4DFF6B4A), RoundedCornerShape(7.dp))
                    .clickable { onConfirm() }
                    .padding(horizontal = 14.dp, vertical = 6.dp)
            ) {
                Text("知道了", fontSize = 12.sp, color = Color.White)
            }
        }
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text,
        fontSize = 13.sp,
        fontWeight = FontWeight.Bold,
        color = Color(0xFFA9A9A9),
        modifier = Modifier.padding(top = 8.dp, bottom = 2.dp)
    )
}

private fun subtitleOf(s: SkinStore.Skin): String = buildString {
    if (s.is4D) append("4D · 稀有度 ").append(s.rarity) else append("照片")
    if (s.vip) append(" · VIP")
    if (!s.usable) append(" · 缺文件")
    append(" · ").append(s.path)
    append(" · ").append(if (s.size >= 1024) "${s.size / 1024}KB" else "${s.size}B")
    if (s.hasBackup) append(" · 已备份")
}

@Composable
private fun PickerRow(
    title: String,
    tag: String,
    tagColor: Color,
    subtitle: String,
    selected: Boolean,
    dim: Boolean,
    trailing: String = "",
    onClick: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .background(
                if (selected) SAccent.copy(alpha = 0.30f) else SCard,
                RoundedCornerShape(9.dp)
            )
            .clickable(enabled = !dim) { onClick() }
            .padding(horizontal = 10.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {

        if (tag.isNotEmpty()) {
            Box(
                Modifier
                    .padding(end = 8.dp)
                    .background(tagColor.copy(alpha = 0.22f), RoundedCornerShape(5.dp))
                    .padding(horizontal = 5.dp, vertical = 1.dp)
            ) {
                Text(tag, fontSize = 9.sp, color = tagColor)
            }
        }
        Column(Modifier.weight(1f)) {
            Text(
                (if (selected) "● " else "") + title,
                fontSize = 13.sp,
                color = if (dim) Color(0x66FFFFFF) else Color.White,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                subtitle,
                fontSize = 10.sp,
                color = SMuted,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
        if (trailing.isNotEmpty()) {
            Text(
                trailing,
                fontSize = 10.sp,
                color = SAccent,
                modifier = Modifier.padding(start = 6.dp)
            )
        } else if (selected) {
            Box(
                Modifier
                    .background(SGreen.copy(alpha = 0.25f), RoundedCornerShape(6.dp))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            ) {
                Text("已选", fontSize = 9.sp, color = SGreen)
            }
        }
    }
}

@Composable
private fun BigButton(
    text: String,
    enabled: Boolean = true,
    color: Color = SAccent.copy(alpha = 0.8f),
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .fillMaxWidth()
            .padding(top = 4.dp)
            .background(if (enabled) color else Color(0x14FFFFFF), RoundedCornerShape(10.dp))
            .clickable(enabled = enabled) { onClick() }
            .padding(vertical = 10.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text,
            fontSize = 13.sp,
            fontWeight = FontWeight.Bold,
            color = if (enabled) Color.White else Color(0x55FFFFFF)
        )
    }
}
