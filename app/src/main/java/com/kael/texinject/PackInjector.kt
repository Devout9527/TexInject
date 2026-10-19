/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 * TexInject is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * TexInject is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with TexInject. If not, see <https://www.gnu.org/licenses/>.
 */
package com.kael.texinject

import android.content.Context
import android.system.Os
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.nio.channels.FileChannel
import java.nio.file.StandardOpenOption
import java.security.MessageDigest
import java.util.zip.ZipFile

/**
 * 材质注入（纯 Kotlin，不依赖任何内置 so）。
 *
 * 两个版本的落点：
 *
 *   3.9 ：<files>/games/com.netease/resource_packs/<...>_0_0_2
 *          —— 内置补丁包（包内打包的是 textures/sfxs 等）
 *   3.10：<files>/../netease_client_packs/pack_<md5>
 *          —— 原版资源整包（manifest.json 里 name = vanilla_netease_*，
 *             含 textures / models / shaders / effects，约 140 MB）
 *
 * 注入方式（两边一致）：
 *   1. 把 .mcpack/.zip 解开，覆盖到目标包目录；
 *   2. 覆盖前把原文件备份到 <files>/TexInject/Backup/<包名>/；
 *   3. 同步更新目标包里的 folder_md5.json（游戏用它做资源完整性校验，
 *      改了 png 却不改 md5 会被当成损坏包直接忽略）。
 *
 * 不碰目标包根的 manifest.json / pack_manifest.json：那是包的身份证
 * （uuid/名字/版本），改了游戏会当成另一个包，反而加载不到。
 */
object PackInjector {

    /** 3.10 的客户端包目录（与 files 同级）。 */
    private const val DIR_310 = "netease_client_packs"

    /** 3.9 的资源包目录（相对 files）。 */
    private const val DIR_39 = "games/com.netease/resource_packs"

    /** 备份根目录（相对 files）。 */
    private const val BACKUP_ROOT = "TexInject/Backup"

    private const val IDX = "_inject_index.json"

    /** 追加式日志：每行 `b<TAB>路径`（已快照）或 `a<TAB>路径`（注入新增）。崩溃也不丢。 */
    private const val IDX_JOURNAL = "_inject_index.jsonl"

    /** 实测的 3.10 正式版资源包目录（pack_<md5>）；不存在时才按分数兜底选。 */
    private const val PREFERRED_310 = "pack_440399675dfa1db44a7a4be4147bc5c0"

    /** 小于这个体积的条目整块读入内存做 md5 比对；更大的走流式。 */
    private const val MAX_IN_MEMORY = 8L * 1024 * 1024

    /** 材质包里的资源根目录；用来判断"外面是否真的多套了一层"。 */
    private val RES_ROOTS = setOf(
        "textures", "texts", "sounds", "models", "shaders", "effects", "font",
        "particles", "renderer", "ui", "entity", "attachables", "blocks", "items",
        "animations", "animation_controllers", "materials", "fogs", "player", "splashes"
    )

    data class Result(val ok: Boolean, val summary: String, val lines: List<String>)

    /** 找到的注入目标。kind = "3.10" / "3.9"。 */
    data class Target(val dir: File, val kind: String)

    // ------------------------------------------------------------------ 定位

    /**
     * 找注入目标。
     *
     * 先看游戏版本号（3.10 起资源包搬到 netease_client_packs），
     * 版本号拿不到、或对应目录不存在时再用目录探测兜底，两边都不冲突。
     */
    fun findTarget(): Target? {
        val files = GamePaths.filesDir
        val root = files.parentFile ?: files

        val v310 = find310(File(root, DIR_310))
        val v39 = find39(File(files, DIR_39))

        return when (versionPrefers310()) {
            true -> v310 ?: v39
            false -> v39 ?: v310
            null -> v310 ?: v39
        }
    }

    /** 3.10+ → true；3.9 及以下 → false；版本号不可用 → null。 */
    private fun versionPrefers310(): Boolean? {
        val m = Regex("(\\d+)\\.(\\d+)").find(GamePaths.version) ?: return null
        val major = m.groupValues[1].toIntOrNull() ?: return null
        val minor = m.groupValues[2].toIntOrNull() ?: return null
        return major > 3 || (major == 3 && minor >= 10)
    }

    private fun find310(dir: File): Target? {
        if (!dir.isDirectory) return null
        val preferred = File(dir, PREFERRED_310)
        if (preferred.isDirectory && score(preferred) > 0) return Target(preferred, "3.10")
        val best = dir.listFiles { f -> f.isDirectory }
            ?.maxByOrNull { score(it) }
        return if (best != null && score(best) > 0) Target(best, "3.10") else null
    }

    private fun find39(dir: File): Target? {
        if (!dir.isDirectory) return null
        val best = dir.listFiles { f -> f.isDirectory && f.name.contains("FirstPatch", true) }
            ?.maxByOrNull { score(it) }
        return if (best != null && score(best) > 0) Target(best, "3.9") else null
    }

    private fun score(d: File): Int {
        var s = 0
        if (File(d, "folder_md5.json").isFile) s += 4
        if (File(d, "textures").isDirectory) s += 2
        val mf = File(d, "manifest.json").takeIf { it.isFile }
            ?: File(d, "pack_manifest.json").takeIf { it.isFile }
        if (mf != null) {
            s += 1
            runCatching {
                val name = JSONObject(mf.readText())
                    .optJSONObject("header")?.optString("name").orEmpty()
                if (name.contains("vanilla", true)) s += 8
                if (name.contains("FirstPatch", true)) s += 8
            }
        }
        return s
    }

    // ------------------------------------------------------------------ 注入

    fun inject(context: Context, sourceDir: File, packs: List<File>): Result {
        if (packs.isEmpty()) {
            return Result(false, "源目录无材质包：${sourceDir.absolutePath}", emptyList())
        }
        GamePaths.init(context)
        val target = findTarget()
            ?: return Result(false, "未找到资源包目录（3.9 / 3.10 都不存在）", emptyList())

        val t0 = System.currentTimeMillis()
        com.kael.texinject.TexInjectHookInit.logExternal(
            "[TexInject/Pack] inject start: ${target.kind} ${target.dir.name}, packs=${packs.size}"
        )
        val md5File = File(target.dir, "folder_md5.json")
        val map = readMd5Map(md5File)
        val sep = if (map.keys.firstOrNull()?.contains('\\') == true) '\\' else '/'

        val bk = backupDir(target.dir.name)
        bk.mkdirs()
        // 快照只做一次，之后永不写入。所以这里的写入一律走 tmp + rename，
        // 硬链接进备份的原 inode 不会被就地覆盖。
        snapshotOnce(md5File, File(bk, "folder_md5.json"))
        val idx = loadIndex(bk)

        val lines = ArrayList<String>()
        var written = 0
        var skipped = 0
        var backedUp = 0

        for (zip in packs) {
            try {
                var n = 0
                var sk = 0
                // ZipFile 走中央目录：entry.size 才是准的（ZipInputStream 常给 -1，
                // 会让"内容没变就跳过"整个失效，每次都重写全包）。
                ZipFile(zip).use { zf ->
                    val entries = zf.entries().asSequence().filter { !it.isDirectory }.toList()
                    val prefix = commonPrefix(entries.map { it.name })
                    for (e in entries) {
                        // normalize 已拒绝绝对路径和 "..", 这里不再每个 entry 调 canonicalPath（很贵的 syscall）
                        val rel = normalize(e.name, prefix) ?: continue
                        val dst = File(target.dir, rel)
                        val key = if (sep == '\\') rel.replace('/', '\\') else rel
                        if (e.size in 0L..MAX_IN_MEMORY) {
                            // 增量：先算包里这份内容的 md5，跟 folder_md5.json 里现存的比对。
                            // 一样就完全跳过 —— 不备份、不写盘、不改 md5（重复注入几乎没有写 I/O）。
                            val data = zf.getInputStream(e).use { it.readBytes() }
                            val newMd5 = md5(data)
                            if (dst.isFile && dst.length() == data.size.toLong() && map[key] == newMd5) {
                                sk++
                            } else {
                                if (recordOriginal(bk, idx, dst, rel)) backedUp++
                                writeAtomic(dst, data)
                                map[key] = newMd5
                                n++
                            }
                        } else {
                            // 超大文件走流式，边写边算 md5，不整块进内存
                            if (recordOriginal(bk, idx, dst, rel)) backedUp++
                            val digest = MessageDigest.getInstance("MD5")
                            zf.getInputStream(e).use { ins ->
                                writeAtomicStream(dst) { out ->
                                    val buf = ByteArray(1 shl 16)
                                    while (true) {
                                        val r = ins.read(buf)
                                        if (r < 0) break
                                        digest.update(buf, 0, r)
                                        out.write(buf, 0, r)
                                    }
                                }
                            }
                            map[key] = hex(digest.digest())
                            n++
                        }
                    }
                }
                written += n
                skipped += sk
                lines += if (sk > 0) "OK ${zip.name}：写入 $n，跳过 $sk（内容未变）"
                else "OK ${zip.name}：$n 个文件"
                com.kael.texinject.TexInjectHookInit.logExternal(
                    "[TexInject/Pack] ${zip.name}: write=$n skip=$sk elapsed=${System.currentTimeMillis() - t0}ms"
                )
            } catch (t: Throwable) {
                lines += "ERR ${zip.name}：${t.message}"
            }
        }

        // 全是跳过时不动 folder_md5.json：省掉一次几十 MB 的读写
        if (written > 0) writeMd5Map(md5File, map)
        if (written > 0 || backedUp > 0) saveIndex(bk, idx)
        com.kael.texinject.TexInjectHookInit.logExternal(
            "[TexInject/Pack] inject done: write=$written skip=$skipped backup=$backedUp " +
                "elapsed=${System.currentTimeMillis() - t0}ms"
        )

        val ms = System.currentTimeMillis() - t0
        val ok = written > 0 || skipped > 0
        val summary = when {
            !ok -> "注入失败：没有写入任何文件"
            written == 0 -> "内容已是最新（跳过 $skipped 个文件），无需写入"
            else -> "注入完成（${packs.size} 个包 / 写 $written 跳过 $skipped / 备份 $backedUp），" +
                "耗时 ${String.format("%.1f", ms / 1000.0)}s，请重启游戏"
        }
        lines += "目标：${target.kind} ${target.dir.absolutePath}"
        lines += "游戏版本：${GamePaths.version.ifEmpty { "未知" }}（${GamePaths.pkg}）"
        return Result(ok, summary, lines)
    }

    // ------------------------------------------------------------------ 还原

    fun restore(context: Context): Result {
        GamePaths.init(context)
        val target = findTarget()
            ?: return Result(false, "未找到资源包目录", emptyList())
        val bk = backupDir(target.dir.name)
        val idx = loadIndex(bk)
        if (idx.backed.isEmpty() && idx.added.isEmpty()) {
            return Result(false, "没有注入备份，无需还原", emptyList())
        }
        val t0 = System.currentTimeMillis()

        // 1. 先删掉注入时新增的文件（这些备份里没有，必须按索引删）
        var removed = 0
        for (rel in idx.added) {
            val f = File(target.dir, rel)
            if (f.isFile && f.delete()) removed++
            cleanEmptyParents(f.parentFile, target.dir)
        }
        // 2. 备份目录整棵树一次性覆盖回去（比逐条查索引快，也不依赖索引完整性）
        val restored = copyTree(bk, target.dir, setOf(IDX, IDX_JOURNAL, "folder_md5.json"))
        // 3. folder_md5.json 整份还原（原文件）
        val md5Bk = File(bk, "folder_md5.json")
        if (md5Bk.isFile) {
            copyBack(md5Bk, File(target.dir, "folder_md5.json"))
        }

        idx.backed.clear()
        idx.added.clear()
        saveIndex(bk, idx)

        val ms = System.currentTimeMillis() - t0
        return Result(true, "已还原官方材质（恢复 $restored 个、删 $removed 个），" +
            "耗时 ${String.format("%.1f", ms / 1000.0)}s，请重启游戏",
            listOf(
                "OK 还原 $restored 个文件，删除新增 $removed 个",
                "目标：${target.kind} ${target.dir.absolutePath}",
                "游戏版本：${GamePaths.version.ifEmpty { "未知" }}（${GamePaths.pkg}）"
            ))
    }

    /** 把备份目录整棵树覆盖回目标目录（跳过索引文件本身）。 */
    private fun copyTree(src: File, dst: File, skipNames: Set<String>): Int {
        val children = src.listFiles() ?: return 0
        var n = 0
        for (c in children) {
            if (c.name in skipNames) continue
            val d = File(dst, c.name)
            if (c.isDirectory) {
                d.mkdirs()
                n += copyTree(c, d, skipNames)
            } else {
                if (copyBack(c, d)) n++
            }
        }
        return n
    }

    // ------------------------------------------------------------------ 状态

    fun targetHasContent(): Boolean {
        val t = findTarget() ?: return false
        return File(t.dir, "textures").isDirectory
    }

    fun backupExists(): Boolean {
        val t = findTarget() ?: return false
        val idx = loadIndex(backupDir(t.dir.name))
        return idx.backed.isNotEmpty() || idx.added.isNotEmpty()
    }

    // ------------------------------------------------------------------ 备份索引

    private class Index {
        val backed = LinkedHashSet<String>()
        val added = LinkedHashSet<String>()
    }

    private fun backupDir(packName: String) =
        File(GamePaths.filesDir, "$BACKUP_ROOT/$packName")

    private fun loadIndex(bk: File): Index {
        val idx = Index()
        val f = File(bk, IDX)
        if (f.isFile) runCatching {
            val j = JSONObject(f.readText())
            j.optJSONArray("backed")?.let { a -> for (i in 0 until a.length()) idx.backed.add(a.getString(i)) }
            j.optJSONArray("added")?.let { a -> for (i in 0 until a.length()) idx.added.add(a.getString(i)) }
        }
        // journal 补齐 json 之后追加、还没落盘的部分
        runCatching {
            val jl = File(bk, IDX_JOURNAL)
            if (jl.isFile) jl.forEachLine { line ->
                val t = line.indexOf('\t')
                if (t > 0) {
                    val rel = line.substring(t + 1)
                    when (line.substring(0, t)) {
                        "a" -> idx.added.add(rel)
                        "b" -> idx.backed.add(rel)
                    }
                }
            }
        }
        return idx
    }

    private fun saveIndex(bk: File, idx: Index) {
        runCatching {
            val j = JSONObject()
            j.put("backed", JSONArray(idx.backed.toList()))
            j.put("added", JSONArray(idx.added.toList()))
            writeAtomic(File(bk, IDX), j.toString().toByteArray(Charsets.UTF_8))
            File(bk, IDX_JOURNAL).delete()
        }
    }

    /**
     * 原件快照，只做一次，之后永不写入。
     *
     * 同分区优先硬链接：零拷贝、零额外占用。备份和目标共用同一个 inode，
     * 所以后面所有覆盖必须走 writeAtomic（tmp + rename），原 inode 才不会被就地改写。
     * 跨分区（SD 卡 FUSE）硬链接会失败，自动退回复制。
     *
     * @return 本次是否新建了快照（false = 之前已有，没动）
     */
    private fun snapshotOnce(src: File, dst: File): Boolean {
        if (dst.exists()) return false
        dst.parentFile?.mkdirs()
        if (runCatching { Os.link(src.path, dst.path) }.isSuccess) return true
        return runCatching {
            src.copyTo(dst, overwrite = false)
            true
        }.getOrDefault(false)
    }

    /** 同一个 inode（说明目标就是硬链接出来的备份本体）。 */
    private fun sameInode(a: File, b: File): Boolean = runCatching {
        val sa = Os.stat(a.path)
        val sb = Os.stat(b.path)
        sa.st_ino == sb.st_ino && sa.st_dev == sb.st_dev
    }.getOrDefault(false)

    /** 记录改写前的原始状态：首次遇到就快照，并把动作追加进 journal（中途崩溃也不丢索引）。 */
    private fun recordOriginal(bk: File, idx: Index, dst: File, rel: String): Boolean {
        if (!dst.exists()) {
            if (idx.added.add(rel)) journalAppend(bk, "a", rel)
            return false
        }
        val fresh = snapshotOnce(dst, File(bk, rel))
        if (idx.backed.add(rel)) journalAppend(bk, "b", rel)
        return fresh
    }

    private fun journalAppend(bk: File, kind: String, rel: String) {
        runCatching {
            FileOutputStream(File(bk, IDX_JOURNAL), true).use { out ->
                out.write("$kind\t$rel\n".toByteArray(Charsets.UTF_8))
            }
        }
    }

    /** 写临时文件再 rename：目标原 inode 保持不动，硬链接出来的备份才是"最初的样子"。 */
    private fun writeAtomic(dst: File, data: ByteArray) {
        writeAtomicStream(dst) { it.write(data) }
    }

    private inline fun writeAtomicStream(dst: File, body: (java.io.OutputStream) -> Unit) {
        dst.parentFile?.mkdirs()
        val tmp = File(dst.parentFile, dst.name + ".ti_tmp")
        FileOutputStream(tmp, false).use { raw ->
            java.io.BufferedOutputStream(raw, 1 shl 16).use { out -> body(out) }
        }
        if (!runCatching { Os.rename(tmp.path, dst.path) }.isSuccess) {
            runCatching { tmp.copyTo(dst, overwrite = true) }
            tmp.delete()
        }
    }

    /** 备份 → 目标。同 inode 直接跳过（否则 TRUNCATE 会把备份本身清空）。 */
    private fun copyBack(src: File, dst: File): Boolean {
        if (sameInode(src, dst)) return true
        return runCatching {
            dst.parentFile?.mkdirs()
            FileChannel.open(src.toPath(), StandardOpenOption.READ).use { i ->
                FileChannel.open(
                    dst.toPath(),
                    StandardOpenOption.WRITE,
                    StandardOpenOption.CREATE,
                    StandardOpenOption.TRUNCATE_EXISTING
                ).use { o ->
                    val n = i.size()
                    var pos = 0L
                    while (pos < n) {
                        val w = i.transferTo(pos, n - pos, o)
                        if (w <= 0L) break
                        pos += w
                    }
                }
            }
            true
        }.getOrDefault(false)
    }

    // ------------------------------------------------------------------ zip

    /**
     * 若整包都套了一层目录（如 MyPack/textures/...），返回 "MyPack/"。
     *
     * 关键：剥之前必须确认剥完还落在资源根上，否则 `textures/` 这种单根包
     * 会被当成"套了一层"而把 textures 本身剥掉，文件就写到包根去了。
     */
    private fun commonPrefix(names: List<String>): String {
        val valid = names.filterNot { it.startsWith("__MACOSX/") || it.startsWith(".") }
        if (valid.isEmpty()) return ""
        if (valid.any { it == "manifest.json" || it == "pack_manifest.json" }) return ""
        val firsts = valid.map { it.substringBefore('/') }.distinct()
        if (firsts.size != 1 || valid.any { !it.contains('/') }) return ""
        val p = firsts[0]
        if (p.lowercase() in RES_ROOTS) return ""
        val stripped = valid.map { it.substringAfter('/') }
        if (stripped.none { it.substringBefore('/').lowercase() in RES_ROOTS }) return ""
        return "$p/"
    }

    private fun normalize(name: String, prefix: String): String? {
        var n = name.replace('\\', '/')
        if (prefix.isNotEmpty() && n.startsWith(prefix)) n = n.substring(prefix.length)
        n = n.trimStart('/')
        if (n.isEmpty()) return null
        if (n.startsWith("__MACOSX/") || n.startsWith(".")) return null
        if (n.split('/').any { it.isEmpty() || it == ".." || it == "." }) return null
        // 不覆盖包的身份证
        if (n == "manifest.json" || n == "pack_manifest.json" || n == "folder_md5.json") return null
        return n
    }

    // ------------------------------------------------------------------ md5 / json

    private fun md5(f: File): String {
        val d = MessageDigest.getInstance("MD5")
        FileInputStream(f).use { ins ->
            val buf = ByteArray(1 shl 16)
            while (true) {
                val n = ins.read(buf)
                if (n < 0) break
                d.update(buf, 0, n)
            }
        }
        return hex(d.digest())
    }

    private fun md5(data: ByteArray): String =
        hex(MessageDigest.getInstance("MD5").digest(data))

    private fun hex(bytes: ByteArray): String {
        val sb = StringBuilder(bytes.size * 2)
        for (b in bytes) sb.append("%02x".format(b))
        return sb.toString()
    }

    /** folder_md5.json 是 {"路径": "md5", ...}，路径可能是 / 或 \ 分隔，按原样保留。 */
    private fun readMd5Map(f: File): LinkedHashMap<String, String> {
        val map = LinkedHashMap<String, String>()
        if (!f.isFile) return map
        val text = f.readText()
        val re = Regex("\"((?:[^\"\\\\]|\\\\.)*)\"\\s*:\\s*\"([0-9a-fA-F]{32})\"")
        for (m in re.findAll(text)) map[unescape(m.groupValues[1])] = m.groupValues[2]
        return map
    }

    private fun writeMd5Map(f: File, map: Map<String, String>) {
        val sb = StringBuilder(map.size * 72)
        sb.append('{')
        var first = true
        for ((k, v) in map) {
            if (!first) sb.append(", ")
            first = false
            sb.append('"').append(escape(k)).append("\": \"").append(v).append('"')
        }
        sb.append('}')
        writeAtomic(f, sb.toString().toByteArray(Charsets.UTF_8))
    }

    private fun escape(s: String): String {
        val sb = StringBuilder(s.length + 8)
        for (c in s) when (c) {
            '"' -> sb.append("\\\"")
            '\\' -> sb.append("\\\\")
            else -> sb.append(c)
        }
        return sb.toString()
    }

    private fun unescape(s: String): String {
        val sb = StringBuilder(s.length)
        var i = 0
        while (i < s.length) {
            val c = s[i]
            if (c == '\\' && i + 1 < s.length) {
                when (s[i + 1]) {
                    '\\' -> { sb.append('\\'); i += 2 }
                    '"' -> { sb.append('"'); i += 2 }
                    '/' -> { sb.append('/'); i += 2 }
                    else -> { sb.append(c); i++ }
                }
            } else {
                sb.append(c)
                i++
            }
        }
        return sb.toString()
    }

    private fun cleanEmptyParents(dir: File?, stop: File) {
        var d = dir
        while (d != null && d != stop && d.isDirectory &&
            d.listFiles()?.isEmpty() == true) {
            d.delete()
            d = d.parentFile
        }
    }
}
