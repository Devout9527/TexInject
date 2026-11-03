/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 内存读写封装。
 *
 * 底层是 mem/MemRw.cpp（process_vm_readv/writev）。这里只做三件事：
 *   1. 给指针链读写（游戏里几乎全是「基址 + 一串偏移」）
 *   2. 缓存 so 基址，别每次都去遍历 phdr
 *   3. 所有失败都返回 null / 0 / false，调用方不用 try
 */
package com.kael.texinject.nativecore

object Mem {

    /** 游戏引擎的 so 名。 */
    const val GAME_SO = "libminecraftpe.so"

    private val bases = HashMap<String, Long>()

    /** so 基址（带缓存；找不到返回 0）。 */
    fun base(name: String = GAME_SO): Long {
        bases[name]?.let { if (it != 0L) return it }
        val b = runCatching { NativeCore.memBase(name) }.getOrDefault(0L)
        bases[name] = b
        return b
    }

    /** 模块内 RVA 换算成绝对地址。基址拿不到返回 0。 */
    fun at(rva: Long, name: String = GAME_SO): Long {
        val b = base(name)
        return if (b == 0L || rva <= 0L) 0L else b + rva
    }

    fun readInt(addr: Long): Int = runCatching { NativeCore.memReadI32(addr) }.getOrDefault(0)

    fun readLong(addr: Long): Long = runCatching { NativeCore.memReadI64(addr) }.getOrDefault(0L)

    fun readFloat(addr: Long): Float = runCatching { NativeCore.memReadF32(addr) }.getOrDefault(0f)

    /** 读指针（就是 readLong，语义上区分一下）。 */
    fun readPtr(addr: Long): Long = readLong(addr)

    fun readBytes(addr: Long, length: Int): ByteArray? =
        runCatching { NativeCore.memReadBytes(addr, length) }.getOrNull()

    fun readString(addr: Long, max: Int = 128): String? =
        runCatching { NativeCore.memReadCString(addr, max) }.getOrNull()

    fun writeInt(addr: Long, value: Int): Boolean =
        runCatching { NativeCore.memWriteI32(addr, value) }.getOrDefault(false)

    fun writeLong(addr: Long, value: Long): Boolean =
        runCatching { NativeCore.memWriteI64(addr, value) }.getOrDefault(false)

    fun writeFloat(addr: Long, value: Float): Boolean =
        runCatching { NativeCore.memWriteF32(addr, value) }.getOrDefault(false)

    fun writeBytes(addr: Long, data: ByteArray): Boolean =
        runCatching { NativeCore.memWriteBytes(addr, data) }.getOrDefault(false)

    /**
     * 指针链：从 [start] 读一次指针，然后依次加偏移读，最后返回地址。
     *
     *   follow(actor, 0x10, 0x28)   ==  readPtr(readPtr(actor + 0x10) + 0x28)
     *
     * 中间任何一环读到 0 就返回 0。
     */
    fun follow(start: Long, vararg offsets: Long): Long {
        var cur = start
        for (off in offsets) {
            cur = readPtr(cur + off)
            if (cur == 0L) return 0L
        }
        return cur
    }

    /** follow 的最后一跳直接取值。 */
    fun followInt(start: Long, vararg offsets: Long): Int {
        val p = follow(start, *offsets)
        return if (p == 0L) 0 else readInt(p)
    }

    /** 地址是否可读（读 4 字节试试）。 */
    fun readable(addr: Long): Boolean = addr > 0x1000 && runCatching {
        NativeCore.memReadBytes(addr, 4) != null
    }.getOrDefault(false)
}
