package com.kael.texinject.nativecore

object Mem {

    const val GAME_SO = "libminecraftpe.so"

    private val bases = HashMap<String, Long>()

    fun base(name: String = GAME_SO): Long {
        bases[name]?.let { if (it != 0L) return it }
        val b = runCatching { NativeCore.memBase(name) }.getOrDefault(0L)
        bases[name] = b
        return b
    }

    fun at(rva: Long, name: String = GAME_SO): Long {
        val b = base(name)
        return if (b == 0L || rva <= 0L) 0L else b + rva
    }

    fun readInt(addr: Long): Int = runCatching { NativeCore.memReadI32(addr) }.getOrDefault(0)

    fun readLong(addr: Long): Long = runCatching { NativeCore.memReadI64(addr) }.getOrDefault(0L)

    fun readFloat(addr: Long): Float = runCatching { NativeCore.memReadF32(addr) }.getOrDefault(0f)

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

    fun follow(start: Long, vararg offsets: Long): Long {
        var cur = start
        for (off in offsets) {
            cur = readPtr(cur + off)
            if (cur == 0L) return 0L
        }
        return cur
    }

    fun followInt(start: Long, vararg offsets: Long): Int {
        val p = follow(start, *offsets)
        return if (p == 0L) 0 else readInt(p)
    }

    fun readable(addr: Long): Boolean = addr > 0x1000 && runCatching {
        NativeCore.memReadBytes(addr, 4) != null
    }.getOrDefault(false)
}
