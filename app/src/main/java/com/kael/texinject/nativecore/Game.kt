package com.kael.texinject.nativecore

object Game {

    const val CAMERA_CLIP = "cameraClip"

    const val KILL_AURA = "killAura"

    fun setFeature(id: String, on: Boolean): Boolean =
        runCatching { NativeCore.gameFeature(id, on) }.getOrDefault(false)

    fun isOn(id: String): Boolean =
        runCatching { NativeCore.gameFeatureOn(id) }.getOrDefault(false)

    fun attach(): Boolean = runCatching { NativeCore.gameAttach() }.getOrDefault(false)

    fun inWorld(): Boolean = runCatching { NativeCore.gameInWorld() }.getOrDefault(false)

    fun handDamage(): Int? =
        runCatching { NativeCore.gameHandDamage() }.getOrNull()?.takeIf { it != Int.MIN_VALUE }

    fun setHandDamage(value: Int): Boolean =
        runCatching { NativeCore.gameSetDamage(value) }.getOrDefault(false)

    fun handInfo(): String =
        runCatching { NativeCore.gameHandInfo() }.getOrDefault("native 未加载")

    fun applyNbt(node: com.kael.texinject.nbt.Snbt): Boolean =
        runCatching { NativeCore.gameApplyNbt(flatten(node)) }.getOrDefault(false)

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
