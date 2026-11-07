package com.kael.texinject

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.io.File

object SkinStore {

    private const val TAG = "TexInject/Skin"

    data class Skin(
        val id: String,
        val name: String,
        val slogan: String,
        val rarity: Int,
        val vip: Boolean,

        val path: String,
        val isPack: Boolean,
        val secondType: Int,
        val modVersion: String,

        val size: Long,
        val hasBackup: Boolean,
    ) {

        val is4D: Boolean get() = isPack || path.endsWith(".zip", true)

        val isShadow: Boolean get() = !is4D

        val usable: Boolean get() = size > 0

        val kind: String get() = if (is4D) "4D" else "照片"
    }

    data class Result(val ok: Boolean, val summary: String)

    fun recordsDir(c: Context): File =
        File(c.filesDir, "games/com.netease/storge/stream/resource_management/skin_records")

    fun packsDir(c: Context): File = File(c.filesDir, "games/com.netease/skin_packs")

    fun usersDir(c: Context): File = File(c.filesDir, "games/com.netease/storge/stream/users")

    fun backupDir(c: Context): File =
        File(c.filesDir, "TexInject/Backup/skin").apply { mkdirs() }

    fun slotBackupDir(c: Context, id: String): File =
        File(backupDir(c), id)

    fun recordFile(c: Context, id: String): File = File(recordsDir(c), "$id.json")

    fun resourceFile(c: Context, path: String, id: String = ""): File {
        val name = path.ifEmpty { if (id.isNotEmpty()) "$id.zip" else "" }
        return File(packsDir(c), name)
    }

    fun hasBackup(c: Context, id: String): Boolean =
        File(slotBackupDir(c, id), "record.json").isFile

    fun list(c: Context): List<Skin> {
        val jsons = recordsDir(c).listFiles { f -> f.isFile && f.name.endsWith(".json") }
            ?: return emptyList()
        val out = ArrayList<Skin>(jsons.size)
        for (j in jsons) {
            val id = j.name.removeSuffix(".json")
            val obj = try {
                JSONObject(j.readText())
            } catch (t: Throwable) {
                Log.w(TAG, "读不出 $id: ${t.message}")
                continue
            }
            val path = obj.optString("path").ifEmpty { "$id.zip" }
            val f = resourceFile(c, path, id)

            out += Skin(
                id = id,
                name = obj.optString("name").ifEmpty { id },
                slogan = obj.optString("slogan"),
                rarity = obj.optInt("rarity", 0),
                vip = obj.optBoolean("vip_only", false),
                path = path,
                isPack = obj.optBoolean("is_pack", false),
                secondType = obj.optInt("second_type", 0),
                modVersion = obj.optString("mod_version"),
                size = if (f.isFile) f.length() else 0L,
                hasBackup = hasBackup(c, id),
            )
        }
        return out.sortedWith(
            compareByDescending<Skin> { it.rarity }
                .thenByDescending { it.is4D }
                .thenBy { it.name }
        )
    }

    fun uids(c: Context): List<String> =
        usersDir(c).listFiles { f -> f.isDirectory }
            ?.map { it.name }
            ?.filter { it.matches(Regex("[0-9]+")) }
            ?.sorted()
            ?: emptyList()

    fun skinOf(c: Context, uid: String): String {
        if (uid.isEmpty()) return ""
        val txt = File(usersDir(c), "$uid/skin.txt")
        if (!txt.isFile) return ""
        return try {
            txt.readText().trim()
        } catch (t: Throwable) {
            ""
        }
    }

    fun recentUid(c: Context): String {
        var best = ""
        var bestTime = 0L
        for (uid in uids(c)) {
            val txt = File(usersDir(c), "$uid/skin.txt")
            if (!txt.isFile) continue
            if (txt.lastModified() >= bestTime) {
                best = uid
                bestTime = txt.lastModified()
            }
        }
        return best
    }

    fun equipped(c: Context, known: List<Skin> = list(c)): String {
        val ids = known.map { it.id }.toHashSet()
        var best = ""
        var bestTime = 0L
        val dirs = usersDir(c).listFiles { f -> f.isDirectory } ?: return ""
        for (u in dirs) {
            val txt = File(u, "skin.txt")
            if (!txt.isFile) continue
            val id = try {
                txt.readText().trim()
            } catch (t: Throwable) {
                continue
            }
            if (id in ids && txt.lastModified() >= bestTime) {
                best = id
                bestTime = txt.lastModified()
            }
        }
        return best
    }

    fun recentIds(c: Context): List<String> {
        val dirs = usersDir(c).listFiles { f -> f.isDirectory } ?: return emptyList()
        val out = LinkedHashSet<String>()
        val re = Regex("([0-9]{6,})\\.(zip|png)")
        for (u in dirs) {
            val f = File(u, "custom_packs_md5_info")
            if (!f.isFile) continue
            val text = try {
                f.readText()
            } catch (t: Throwable) {
                continue
            }
            re.findAll(text).forEach { m -> out += m.groupValues[1] }
        }
        return out.toList()
    }

    fun swap(c: Context, targetId: String, sourceId: String): Result {
        if (targetId == sourceId) return Result(false, "两个是同一个皮肤")
        val all = list(c)
        val t = all.firstOrNull { it.id == targetId } ?: return Result(false, "找不到 $targetId 的记录")
        val s = all.firstOrNull { it.id == sourceId } ?: return Result(false, "找不到 $sourceId 的记录")
        if (!t.usable) return Result(false, "槽位缺少资源文件：${t.path}")
        if (!s.usable) return Result(false, "来源缺少资源文件：${s.path}")

        val tRes = resourceFile(c, t.path, t.id)
        val sRes = resourceFile(c, s.path, s.id)

        val newExt = sRes.extension
        val newResName = if (tRes.extension.equals(newExt, true)) {
            tRes.name
        } else {
            "$targetId.$newExt"
        }

        ensureBackup(c, t) ?: return Result(false, "备份失败，未改动")

        try {
            sRes.copyTo(File(packsDir(c), newResName), overwrite = true)
        } catch (e: Throwable) {
            Log.e(TAG, "覆盖失败 $targetId: ${e.message}")
            runCatching { restore(c, targetId) }
            return Result(false, "覆盖失败：" + (e.message ?: e.javaClass.simpleName))
        }

        val crossKind = t.is4D != s.is4D
        if (crossKind) {
            val ok = try {
                val obj = JSONObject(recordFile(c, t.id).readText())
                obj.put("path", newResName)
                obj.put("is_pack", s.is4D)
                recordFile(c, t.id).writeText(obj.toString(2))
                true
            } catch (e: Throwable) {
                Log.e(TAG, "改记录失败 $targetId: ${e.message}")
                false
            }
            if (!ok) {
                runCatching { restore(c, targetId) }
                return Result(false, "改记录失败，已回滚")
            }
        }

        if (tRes.name != newResName) {
            runCatching { tRes.delete() }
        }

        runCatching { syncHashes(c, File(packsDir(c), newResName)) }

        Log.i(
            TAG,
            "换肤 $targetId(${t.kind}) <- $sourceId(${s.kind}) → $newResName" +
                if (crossKind) " [跨类，已改 is_pack/path]" else ""
        )
        return Result(
            true,
            "已替换：${t.name} ← ${s.name}" +
                if (crossKind) "（${t.kind} → ${s.kind}）" else ""
        )
    }

    fun restore(c: Context, targetId: String): Result {
        val dir = slotBackupDir(c, targetId)
        val bakRecord = File(dir, "record.json")
        if (!bakRecord.isFile) return Result(false, "没有备份，无需还原")
        val name = nameOf(c, targetId)
        return try {

            val bakObj = JSONObject(bakRecord.readText())
            val bakPath = bakObj.optString("path").ifEmpty { "$targetId.zip" }
            val bakRes = dir.listFiles()?.firstOrNull {
                it.isFile && it.name.startsWith("resource.")
            }
            if (bakRes != null) {
                bakRes.copyTo(File(packsDir(c), bakPath), overwrite = true)
            }

            packsDir(c).listFiles { f -> f.isFile && f.name.startsWith("$targetId.") }
                ?.filter { it.name != bakPath }
                ?.forEach { runCatching { it.delete() } }
            bakRecord.copyTo(recordFile(c, targetId), overwrite = true)
            runCatching { syncHashes(c, File(packsDir(c), bakPath)) }
            Log.i(TAG, "还原皮肤 $targetId")
            Result(true, "已还原：$name")
        } catch (e: Throwable) {
            Result(false, "还原失败：" + (e.message ?: e.javaClass.simpleName))
        }
    }

    fun restoreAll(c: Context): Result {
        val dirs = backupDir(c).listFiles { f -> f.isDirectory } ?: return Result(false, "没有备份")
        var n = 0
        for (d in dirs) {
            if (restore(c, d.name).ok) n++
        }
        return Result(n > 0, if (n > 0) "已还原 $n 个皮肤" else "一个都没还原成功")
    }

    private fun ensureBackup(c: Context, t: Skin): File? {
        val dir = slotBackupDir(c, t.id)
        val bakRecord = File(dir, "record.json")
        if (bakRecord.isFile) return dir
        return try {
            dir.mkdirs()
            val rec = recordFile(c, t.id)
            val res = resourceFile(c, t.path, t.id)
            rec.copyTo(bakRecord, overwrite = true)
            res.copyTo(File(dir, "resource." + res.extension), overwrite = true)

            val bakRes = File(dir, "resource." + res.extension)
            if (bakRecord.length() != rec.length() || bakRes.length() != res.length()) {
                dir.deleteRecursively()
                Log.e(TAG, "备份校验失败 ${t.id}")
                null
            } else {
                dir
            }
        } catch (e: Throwable) {
            Log.e(TAG, "备份失败 ${t.id}: ${e.message}")
            dir.deleteRecursively()
            null
        }
    }

    private fun nameOf(c: Context, id: String): String = try {
        JSONObject(recordFile(c, id).readText()).optString("name", id)
    } catch (t: Throwable) {
        id
    }

    private fun syncHashes(c: Context, file: File) {
        val sha1 = sha1Of(file) ?: return
        val users = usersDir(c).listFiles { f -> f.isDirectory } ?: return
        val re = Regex("[0-9a-f]{40}")
        for (u in users) {
            val f = File(u, "custom_packs_md5_info")
            if (!f.isFile) continue
            val text = try {
                f.readText()
            } catch (t: Throwable) {
                continue
            }
            val at = text.indexOf(file.name).takeIf { it >= 0 } ?: continue
            val m = re.find(text, at) ?: continue
            val fixed = text.substring(0, m.range.first) + sha1 + text.substring(m.range.last + 1)
            if (fixed == text) continue
            try {
                f.writeText(fixed)
            } catch (t: Throwable) {
                Log.w(TAG, "同步 ${f.name} 失败: ${t.message}")
            }
        }
    }

    private fun sha1Of(f: File): String? = try {
        val md = java.security.MessageDigest.getInstance("SHA-1")
        f.inputStream().use { ins ->
            val buf = ByteArray(64 * 1024)
            while (true) {
                val n = ins.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
            }
        }
        md.digest().joinToString("") { "%02x".format(it) }
    } catch (t: Throwable) {
        Log.w(TAG, "算 sha1 失败: ${t.message}")
        null
    }
}
