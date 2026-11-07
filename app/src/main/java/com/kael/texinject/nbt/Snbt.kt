package com.kael.texinject.nbt

sealed class Snbt {

    class Compound(val entries: LinkedHashMap<String, Snbt> = LinkedHashMap()) : Snbt()

    class List(val items: MutableList<Snbt> = ArrayList()) : Snbt()

    class Str(var value: String) : Snbt()

    class Num(var text: String, var suffix: Char = '\u0000') : Snbt()

    fun display(): String = when (this) {
        is Compound -> "{${entries.size}}"
        is List -> "[${items.size}]"
        is Str -> value
        is Num -> text + if (suffix == '\u0000') "" else suffix.toString()
    }

    fun kindName(): String = when (this) {
        is Compound -> "Compound"
        is List -> "List"
        is Str -> "String"
        is Num -> when (suffix) {
            'b', 'B' -> "Byte"
            's', 'S' -> "Short"
            'L', 'l' -> "Long"
            'f', 'F' -> "Float"
            'd', 'D' -> "Double"
            else -> "Int"
        }
    }

    companion object {

        fun parse(text: String): Snbt? = runCatching {
            val p = Parser(text)
            val node = p.value() ?: return@runCatching null
            p.ws()
            node
        }.getOrNull()

        fun serialize(node: Snbt): String = buildString { write(this, node) }

        private fun write(sb: StringBuilder, node: Snbt) {
            when (node) {
                is Compound -> {
                    sb.append('{')
                    var first = true
                    for ((k, v) in node.entries) {
                        if (!first) sb.append(',')
                        first = false
                        sb.append(k).append(':')
                        write(sb, v)
                    }
                    sb.append('}')
                }
                is List -> {
                    sb.append('[')
                    node.items.forEachIndexed { i, v ->
                        if (i > 0) sb.append(',')
                        write(sb, v)
                    }
                    sb.append(']')
                }
                is Str -> sb.append(quote(node.value))
                is Num -> {
                    sb.append(node.text)
                    if (node.suffix != '\u0000') sb.append(node.suffix)
                }
            }
        }

        private fun quote(s: String): String {
            val sb = StringBuilder(s.length + 2)
            sb.append('"')
            for (c in s) when (c) {
                '"' -> sb.append("\\\"")
                '\\' -> sb.append("\\\\")
                '\n' -> sb.append("\\n")
                '\r' -> sb.append("\\r")
                '\t' -> sb.append("\\t")
                else -> sb.append(c)
            }
            sb.append('"')
            return sb.toString()
        }
    }

    private class Parser(private val src: String) {
        private var pos = 0

        fun ws() {
            while (pos < src.length && src[pos].isWhitespace()) pos++
        }

        fun value(): Snbt? {
            ws()
            if (pos >= src.length) return null
            return when (src[pos]) {
                '{' -> compound()
                '[' -> list()
                '"' -> Str(string()).takeIf { it.value.isNotEmpty() || true }
                else -> {
                    val start = pos
                    if (looksNumeric()) number() else bare(start)
                }
            }
        }

        private fun looksNumeric(): Boolean {
            if (pos >= src.length) return false
            val c = src[pos]
            return c.isDigit() || c == '-' || c == '+' || c == '.'
        }

        private fun compound(): Compound {
            val map = LinkedHashMap<String, Snbt>()
            pos++
            while (true) {
                ws()
                if (pos >= src.length) break
                if (src[pos] == '}') { pos++; break }
                if (src[pos] == ',') { pos++; continue }
                val key = key()
                ws()
                if (pos < src.length && src[pos] == ':') pos++
                val v = value() ?: break
                if (key.isNotEmpty()) map[key] = v
            }
            return Compound(map)
        }

        private fun list(): List {
            val items = ArrayList<Snbt>()
            pos++

            if (pos + 1 < src.length && src[pos] in "IiBbLl" && src[pos + 1] == ';') pos += 2
            while (true) {
                ws()
                if (pos >= src.length) break
                if (src[pos] == ']') { pos++; break }
                if (src[pos] == ',') { pos++; continue }
                val v = value() ?: break
                items.add(v)
            }
            return List(items)
        }

        private fun key(): String {
            ws()
            if (pos < src.length && src[pos] == '"') return string()
            val start = pos
            while (pos < src.length && src[pos] !in ":{}[],") pos++
            return src.substring(start, pos).trim().trim('"')
        }

        private fun string(): String {
            pos++
            val sb = StringBuilder()
            while (pos < src.length) {
                val c = src[pos++]
                if (c == '"') break
                if (c == '\\' && pos < src.length) {
                    when (val e = src[pos++]) {
                        'n' -> sb.append('\n')
                        'r' -> sb.append('\r')
                        't' -> sb.append('\t')
                        else -> sb.append(e)
                    }
                    continue
                }
                sb.append(c)
            }
            return sb.toString()
        }

        private fun number(): Num {
            val start = pos
            if (pos < src.length && (src[pos] == '-' || src[pos] == '+')) pos++
            while (pos < src.length) {
                val c = src[pos]
                if (c.isDigit() || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') pos++ else break
            }
            val text = src.substring(start, pos)
            var suffix = '\u0000'
            if (pos < src.length && src[pos] in "bBsSlLfFdD") {
                suffix = src[pos]
                pos++
            }
            return Num(text, suffix)
        }

        private fun bare(start: Int): Snbt {
            while (pos < src.length && src[pos] !in ",}] \n\r\t") pos++
            return Str(src.substring(start, pos))
        }
    }
}
