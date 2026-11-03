/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 二进制 NBT 读写（MC 存档格式）。
 *
 * 为什么不用 Orange 那个 NbtNode：它解析的是 SNBT 文本（{Foo:1b,Bar:"x"}），
 * 读 level.dat 这种 gzip + 二进制结构会直接解析失败。这里按官方格式实现。
 *
 * 文件通常是 gzip 压缩的，读的时候看魔数判断，写回时保持原样。
 */
package com.kael.texinject.nbt

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.util.zip.GZIPInputStream
import java.util.zip.GZIPOutputStream

sealed class Tag {

    /** 标签类型 id（NBT 规范里 1..12）。 */
    abstract val type: Int

    class TByte(var value: Byte) : Tag() {
        override val type get() = 1
    }

    class TShort(var value: Short) : Tag() {
        override val type get() = 2
    }

    class TInt(var value: Int) : Tag() {
        override val type get() = 3
    }

    class TLong(var value: Long) : Tag() {
        override val type get() = 4
    }

    class TFloat(var value: Float) : Tag() {
        override val type get() = 5
    }

    class TDouble(var value: Double) : Tag() {
        override val type get() = 6
    }

    class TByteArray(var value: ByteArray) : Tag() {
        override val type get() = 7
    }

    class TString(var value: String) : Tag() {
        override val type get() = 8
    }

    class TList(var elemType: Int, val items: MutableList<Tag> = ArrayList()) : Tag() {
        override val type get() = 9
    }

    class TCompound(val entries: LinkedHashMap<String, Tag> = LinkedHashMap()) : Tag() {
        override val type get() = 10
    }

    class TIntArray(var value: IntArray) : Tag() {
        override val type get() = 11
    }

    class TLongArray(var value: LongArray) : Tag() {
        override val type get() = 12
    }

    /** 显示用的短值；容器类型给个摘要。 */
    fun display(): String = when (this) {
        is TByte -> "${value}b"
        is TShort -> "${value}s"
        is TInt -> value.toString()
        is TLong -> "${value}L"
        is TFloat -> "${value}f"
        is TDouble -> value.toString()
        is TString -> value
        is TByteArray -> "[${value.size} 字节]"
        is TIntArray -> "[${value.size} 个 int]"
        is TLongArray -> "[${value.size} 个 long]"
        is TList -> "[${items.size} 项]"
        is TCompound -> "{${entries.size} 项}"
    }

    fun kindName(): String = when (this) {
        is TByte -> "Byte"
        is TShort -> "Short"
        is TInt -> "Int"
        is TLong -> "Long"
        is TFloat -> "Float"
        is TDouble -> "Double"
        is TString -> "String"
        is TByteArray -> "ByteArray"
        is TIntArray -> "IntArray"
        is TLongArray -> "LongArray"
        is TList -> "List"
        is TCompound -> "Compound"
    }
}

object NbtFile {

    /** 读一个 .dat：自动识别 gzip，返回根 Compound。失败返回 null。 */
    fun read(file: File): Tag.TCompound? = runCatching {
        val raw = file.readBytes()
        val body = if (raw.size >= 2 && raw[0] == 0x1F.toByte() && raw[1] == 0x8B.toByte()) {
            GZIPInputStream(ByteArrayInputStream(raw)).use { it.readBytes() }
        } else {
            raw
        }
        val input = DataInputStream(ByteArrayInputStream(body))
        val type = input.readUnsignedByte()
        if (type != 10) {
            return@runCatching null
        }
        readString(input)                 // 根节点的名字，通常是空串
        readPayload(input, 10) as? Tag.TCompound
    }.getOrNull()

    /** 写回，保持 gzip（跟读进来的方式对齐）。 */
    fun write(file: File, root: Tag.TCompound, gzip: Boolean = true) {
        val body = ByteArrayOutputStream()
        DataOutputStream(body).use { out ->
            out.writeByte(10)
            writeString(out, "")
            writePayload(out, root)
        }
        val bytes = body.toByteArray()
        if (gzip) {
            val packed = ByteArrayOutputStream()
            GZIPOutputStream(packed).use { it.write(bytes) }
            file.writeBytes(packed.toByteArray())
        } else {
            file.writeBytes(bytes)
        }
    }

    fun isGzip(file: File): Boolean = runCatching {
        val head = ByteArray(2)
        file.inputStream().use { it.read(head) }
        head.size == 2 && head[0] == 0x1F.toByte() && head[1] == 0x8B.toByte()
    }.getOrDefault(true)

    // ---------------------------------------------------------------- 读写原语

    private fun readPayload(input: DataInputStream, type: Int): Tag? = when (type) {
        1 -> Tag.TByte(input.readByte())
        2 -> Tag.TShort(input.readShort())
        3 -> Tag.TInt(input.readInt())
        4 -> Tag.TLong(input.readLong())
        5 -> Tag.TFloat(input.readFloat())
        6 -> Tag.TDouble(input.readDouble())
        7 -> {
            val n = input.readInt()
            val b = ByteArray(n)
            input.readFully(b)
            Tag.TByteArray(b)
        }
        8 -> Tag.TString(readString(input))
        9 -> {
            val elem = input.readUnsignedByte()
            val n = input.readInt()
            val list = Tag.TList(elem)
            for (i in 0 until n) {
                readPayload(input, elem)?.let { list.items.add(it) }
            }
            list
        }
        10 -> {
            val map = LinkedHashMap<String, Tag>()
            while (true) {
                val t = input.readUnsignedByte()
                if (t == 0) break
                val name = readString(input)
                val child = readPayload(input, t) ?: break
                map[name] = child
            }
            Tag.TCompound(map)
        }
        11 -> {
            val n = input.readInt()
            IntArray(n) { input.readInt() }.let { Tag.TIntArray(it) }
        }
        12 -> {
            val n = input.readInt()
            LongArray(n) { input.readLong() }.let { Tag.TLongArray(it) }
        }
        else -> null
    }

    private fun writePayload(out: DataOutputStream, tag: Tag) {
        when (tag) {
            is Tag.TByte -> out.writeByte(tag.value.toInt())
            is Tag.TShort -> out.writeShort(tag.value.toInt())
            is Tag.TInt -> out.writeInt(tag.value)
            is Tag.TLong -> out.writeLong(tag.value)
            is Tag.TFloat -> out.writeFloat(tag.value)
            is Tag.TDouble -> out.writeDouble(tag.value)
            is Tag.TByteArray -> {
                out.writeInt(tag.value.size)
                out.write(tag.value)
            }
            is Tag.TString -> writeString(out, tag.value)
            is Tag.TList -> {
                out.writeByte(tag.elemType)
                out.writeInt(tag.items.size)
                for (item in tag.items) writePayload(out, item)
            }
            is Tag.TCompound -> {
                for ((name, child) in tag.entries) {
                    out.writeByte(child.type)
                    writeString(out, name)
                    writePayload(out, child)
                }
                out.writeByte(0)
            }
            is Tag.TIntArray -> {
                out.writeInt(tag.value.size)
                for (v in tag.value) out.writeInt(v)
            }
            is Tag.TLongArray -> {
                out.writeInt(tag.value.size)
                for (v in tag.value) out.writeLong(v)
            }
        }
    }

    private fun readString(input: DataInputStream): String {
        val len = input.readUnsignedShort()
        val bytes = ByteArray(len)
        input.readFully(bytes)
        return String(bytes, Charsets.UTF_8)
    }

    private fun writeString(out: DataOutputStream, text: String) {
        val bytes = text.toByteArray(Charsets.UTF_8)
        out.writeShort(bytes.size)
        out.write(bytes)
    }

    // ---------------------------------------------------------------- 编辑

    /** 按路径改一个标量值，路径形如 ["Data", "Player", "Pos"]。返回是否成功。 */
    fun setByPath(root: Tag.TCompound, path: List<String>, text: String): Boolean {
        if (path.isEmpty()) return false
        var node: Tag = root
        for (i in 0 until path.size - 1) {
            node = (node as? Tag.TCompound)?.entries?.get(path[i]) ?: return false
        }
        val parent = node as? Tag.TCompound ?: return false
        val key = path.last()
        when (val old = parent.entries[key] ?: return false) {
            is Tag.TByte -> old.value = text.trim().removeSuffix("b").toByteOrNull() ?: return false
            is Tag.TShort -> old.value = text.trim().removeSuffix("s").toShortOrNull() ?: return false
            is Tag.TInt -> old.value = text.trim().toIntOrNull() ?: return false
            is Tag.TLong -> old.value = text.trim().removeSuffix("L").toLongOrNull() ?: return false
            is Tag.TFloat -> old.value = text.trim().removeSuffix("f").toFloatOrNull() ?: return false
            is Tag.TDouble -> old.value = text.trim().toDoubleOrNull() ?: return false
            is Tag.TString -> old.value = text
            else -> return false     // 容器不在这里改
        }
        return true
    }

    /** 把某个容器的子项拍平成可显示的行。 */
    fun childrenOf(node: Tag): List<Pair<String, Tag>> = when (node) {
        is Tag.TCompound -> node.entries.map { it.key to it.value }
        is Tag.TList -> node.items.mapIndexed { i, t -> "[$i]" to t }
        else -> emptyList()
    }
}
