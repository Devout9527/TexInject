/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cookie 存储：一个文件一条 cookie。
 *   文件名 = cookie 名字
 *   内容   = cookie 数据（sauth_json 原文）
 *
 * 目录（游戏私有外部目录，3.9.15 / 3.10 相同，不随版本变）：
 *   /storage/emulated/0/Android/data/<游戏包名>/files/pack_netease/cookies/
 */
package com.kael.texinject

import android.content.Context
import android.util.Log
import java.io.File

object CookieStore {

    private const val TAG = "TexInject-Cookie"

    /** 相对 filesDir 的子目录。 */
    const val DIR_NAME = "pack_netease/cookies"

    /** 文件名非法字符（Android/ext4 上 / 和 NUL 是硬限制，其余为了可移植一并替换）。 */
    private val ILLEGAL = charArrayOf('/', '\\', ':', '*', '?', '"', '<', '>', '|', '\n', '\r', '\t', '\u0000')

    /** cookie 文件后缀。 */
    const val EXT = ".txt"

    /** 补上 .txt（已经有就不重复加）。 */
    fun withExt(name: String): String {
        val n = name.trim()
        return if (n.endsWith(EXT, ignoreCase = true)) n else n + EXT
    }

    /** 从 sauth_json 内容里取账号 ID（sdkuid / username），取不到返回空。 */
    fun nameFromSauth(text: String): String {
        return try {
            val o = org.json.JSONObject(text)
            val u = o.optString("username", "")
            if (u.isNotEmpty()) u.substringBefore('@') else o.optString("sdkuid", "")
        } catch (t: Throwable) {
            ""
        }
    }

    /** UI 上显示的名字：去掉 .txt。 */
    fun display(name: String): String {
        return if (name.endsWith(EXT, ignoreCase = true)) name.dropLast(EXT.length) else name
    }

    /** 取 cookies 目录，必要时创建。 */
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

    /** 列出所有 cookie 名字（= 文件名），按修改时间倒序（新捕获的在最上面）。 */
    fun list(ctx: Context): List<String> {
        val d = dir(ctx)
        val files = d.listFiles() ?: return emptyList()
        return files.filter { it.isFile }
            .sortedByDescending { it.lastModified() }
            .map { it.name }
    }

    /** 清空目录里所有 cookie；返回删除数量。 */
    fun clearAll(ctx: Context): Int {
        val d = dir(ctx)
        val files = d.listFiles() ?: return 0
        var n = 0
        for (f in files) {
            if (f.isFile && f.delete()) n++
        }
        return n
    }

    /** 读一条；不存在返回 null。 */
    fun read(ctx: Context, name: String): String? {
        return try {
            val f = File(dir(ctx), sanitize(name))
            if (!f.isFile) null else f.readText().trim()
        } catch (t: Throwable) {
            Log.e(TAG, "read fail: " + name, t)
            null
        }
    }

    /** 写一条（覆盖）；返回是否成功。 */
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

    /** 文件名清洗：非法字符换下划线，去掉首尾空白和点，空名回落 "cookie"。 */
    fun sanitize(name: String): String {
        var s = name.trim()
        for (c in ILLEGAL) s = s.replace(c, '_')
        // 允许中英文、数字、常见符号；其余控制字符丢掉
        s = s.filter { it.code >= 0x20 }
            .trim()
            .trim('.')
        if (s.isEmpty()) s = "cookie"
        if (s.length > 120) s = s.substring(0, 120)
        return s
    }

    // ---------------- 导入 / 导出 ----------------

    /** 导入源目录：把文件丢这里，点「导入」全量拷进 cookies/。 */
    fun importDir(ctx: Context): File = subDir(ctx, "pack_netease/cookies_import")

    /** 导出目标目录：点「导出」把选中的 cookie 拷到这里。 */
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

    /**
     * 把 importDir 里的所有文件拷进 cookies 目录。
     * 同名已存在时加 -2/-3 后缀，不覆盖。
     * @return 实际导入数量
     */
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

    /** 把某条 cookie 拷到导出目录；返回导出后的文件路径，失败返回 null。 */
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

    /** 导出目录下所有导出文件的名字（用来在 UI 上提示）。 */
    fun listExported(ctx: Context): List<String> {
        return exportDir(ctx).listFiles()?.filter { it.isFile }?.map { it.name }?.sorted() ?: emptyList()
    }
}
