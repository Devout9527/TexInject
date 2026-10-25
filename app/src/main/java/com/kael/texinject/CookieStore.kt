package com.kael.texinject

import android.content.Context
import android.util.Log
import java.io.File

object CookieStore {

    private const val TAG = "TexInject-Cookie"

    const val DIR_NAME = "pack_netease/cookies"

    private val ILLEGAL = charArrayOf('/', '\\', ':', '*', '?', '"', '<', '>', '|', '\n', '\r', '\t', '\u0000')

    const val EXT = ".txt"

    fun withExt(name: String): String {
        val n = name.trim()
        return if (n.endsWith(EXT, ignoreCase = true)) n else n + EXT
    }

    fun nameFromSauth(text: String): String {
        return try {
            val o = org.json.JSONObject(text)
            val u = o.optString("username", "")
            if (u.isNotEmpty()) u.substringBefore('@') else o.optString("sdkuid", "")
        } catch (t: Throwable) {
            ""
        }
    }

    fun display(name: String): String {
        return if (name.endsWith(EXT, ignoreCase = true)) name.dropLast(EXT.length) else name
    }

    fun dir(ctx: Context): File {
        val base = try {
            ctx.getExternalFilesDir(null)
        } catch (t: Throwable) {
            null
        } ?: ctx.filesDir
        val d = File(base, DIR_NAME)
        if (!d.isDirectory) {
            try {
                d.mkdirs()
            } catch (t: Throwable) {
                Log.e(TAG, "mkdirs fail: " + d.absolutePath, t)
            }
        }
        return d
    }

    fun list(ctx: Context): List<String> {
        val d = dir(ctx)
        val files = d.listFiles() ?: return emptyList()
        return files.filter { it.isFile }
            .sortedByDescending { it.lastModified() }
            .map { it.name }
    }

    fun clearAll(ctx: Context): Int {
        val d = dir(ctx)
        val files = d.listFiles() ?: return 0
        var n = 0
        for (f in files) {
            if (f.isFile && f.delete()) n++
        }
        return n
    }

    fun read(ctx: Context, name: String): String? {
        return try {
            val f = File(dir(ctx), sanitize(name))
            if (!f.isFile) null else f.readText().trim()
        } catch (t: Throwable) {
            Log.e(TAG, "read fail: " + name, t)
            null
        }
    }

    fun write(ctx: Context, name: String, data: String): Boolean {
        return try {
            val f = File(dir(ctx), sanitize(name))
            f.writeText(data)
            true
        } catch (t: Throwable) {
            Log.e(TAG, "write fail: " + name, t)
            false
        }
    }

    fun delete(ctx: Context, name: String): Boolean {
        return try {
            File(dir(ctx), sanitize(name)).delete()
        } catch (t: Throwable) {
            false
        }
    }

    fun rename(ctx: Context, from: String, to: String): Boolean {
        return try {
            File(dir(ctx), sanitize(from)).renameTo(File(dir(ctx), sanitize(to)))
        } catch (t: Throwable) {
            false
        }
    }

    fun sanitize(name: String): String {
        var s = name.trim()
        for (c in ILLEGAL) s = s.replace(c, '_')
        s = s.filter { it.code >= 0x20 }
            .trim()
            .trim('.')
        if (s.isEmpty()) s = "cookie"
        if (s.length > 120) s = s.substring(0, 120)
        return s
    }

    fun importDir(ctx: Context): File = subDir(ctx, "pack_netease/cookies_import")

    fun exportDir(ctx: Context): File = subDir(ctx, "pack_netease/cookies_export")

    private fun subDir(ctx: Context, rel: String): File {
        val base = try {
            ctx.getExternalFilesDir(null)
        } catch (t: Throwable) {
            null
        } ?: ctx.filesDir
        val d = File(base, rel)
        if (!d.isDirectory) {
            try {
                d.mkdirs()
            } catch (t: Throwable) {
                Log.e(TAG, "mkdirs fail: " + d.absolutePath, t)
            }
        }
        return d
    }

    fun importAll(ctx: Context): Int {
        val src = importDir(ctx)
        val files = src.listFiles() ?: return 0
        var n = 0
        for (f in files) {
            if (!f.isFile) continue
            val text = try {
                f.readText()
            } catch (t: Throwable) {
                continue
            }
            if (text.isBlank()) continue
            var name = sanitize(CookieStore.withExt(f.name))
            var i = 2
            while (File(dir(ctx), name).exists() && i < 1000) {
                name = sanitize(CookieStore.withExt(f.name)) + "-" + i
                i++
            }
            if (write(ctx, name, text.trim())) n++
        }
        return n
    }

    fun exportOne(ctx: Context, name: String): String? {
        val data = read(ctx, name) ?: return null
        val out = File(exportDir(ctx), sanitize(name))
        return try {
            out.writeText(data)
            out.absolutePath
        } catch (t: Throwable) {
            null
        }
    }

    fun listExported(ctx: Context): List<String> {
        return exportDir(ctx).listFiles()?.filter { it.isFile }?.map { it.name }?.sorted() ?: emptyList()
    }
}
