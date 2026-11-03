/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 游戏侧开关的 Kotlin 门面。
 *
 * 只剩「相机穿墙」—— 自动工具 / 背包整理 / 设置耐久 / 物品编辑器 / 全局夜视
 * 实测都不好用，已经删干净了，别再往这里加。
 *
 * JNI 调用一律吞异常：面板跑在游戏进程里，一次 UnsatisfiedLinkError
 * 就能把整个悬浮层带走。
 */
package com.kael.texinject.nativecore

object Game {

    /** 相机穿墙：hook 相机避让系统，镜头不再被方块顶回来。 */
    const val CAMERA_CLIP = "cameraClip"

    /** 杀戮光环：自动攻击身边最近的实体（单人/本地才有伤害）。 */
    const val KILL_AURA = "killAura"

    fun setFeature(id: String, on: Boolean): Boolean =
        runCatching { NativeCore.gameFeature(id, on) }.getOrDefault(false)

    fun isOn(id: String): Boolean =
        runCatching { NativeCore.gameFeatureOn(id) }.getOrDefault(false)

    /** 装 LocalPlayer hook，读玩家对象前要先调。 */
    fun attach(): Boolean = runCatching { NativeCore.gameAttach() }.getOrDefault(false)

    /** 手上物品 damage；null 表示读不到（没连接 / 手上是空的）。 */
    fun handDamage(): Int? =
        runCatching { NativeCore.gameHandDamage() }.getOrNull()?.takeIf { it != Int.MIN_VALUE }

    /** 改手上物品 damage。负数合法：没损耗的物品默认就是 0。 */
    fun setHandDamage(value: Int): Boolean =
        runCatching { NativeCore.gameSetDamage(value) }.getOrDefault(false)

    /** 诊断：手上 ItemStack 的地址 + 头 64 字节。 */
    fun handInfo(): String =
        runCatching { NativeCore.gameHandInfo() }.getOrDefault("native 未加载")

    /**
     * 把 SNBT 树挂到手上物品（只写客户端，服务器不认）。
     * List 类型暂不支持 —— 传进来会被跳过。
     */
    fun applyNbt(node: com.kael.texinject.nbt.Snbt): Boolean =
        runCatching { NativeCore.gameApplyNbt(flatten(node)) }.getOrDefault(false)

    /** 把 SNBT 树展平成 native 用的行指令：`类型\t键[\t值]`。 */
    private fun flatten(node: com.kael.texinject.nbt.Snbt): String {
        val sb = StringBuilder()
        fun walk(n: com.kael.texinject.nbt.Snbt) {
            if (n !is com.kael.texinject.nbt.Snbt.Compound) return
            for ((key, value) in n.entries) {
                when (value) {
                    is com.kael.texinject.nbt.Snbt.Compound -> {
                        sb.append("c\t").append(key).append('\n')
                        walk(value)
                        sb.append("e\n")
                    }
                    // List 需要 ListTag 那套构造件，还没接
                    is com.kael.texinject.nbt.Snbt.List -> Unit
                    is com.kael.texinject.nbt.Snbt.Str ->
                        sb.append("s\t").append(key).append('\t').append(value.value).append('\n')
                    is com.kael.texinject.nbt.Snbt.Num ->
                        sb.append(numKind(value.suffix)).append('\t').append(key)
                            .append('\t').append(value.text).append('\n')
                }
            }
        }
        walk(node)
        return sb.toString()
    }

    private fun numKind(suffix: Char): String = when (suffix) {
        'b', 'B' -> "b"
        's', 'S' -> "h"
        'L', 'l' -> "l"
        'f', 'F' -> "f"
        'd', 'D' -> "d"
        else -> "i"
    }
}
