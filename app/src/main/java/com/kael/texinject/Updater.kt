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
import org.json.JSONObject
import java.io.BufferedReader
import java.net.HttpURLConnection
import java.net.URL

/**
 * 更新检查（走 GitHub Release）。
 *
 * 逻辑：
 *   - 拉 /repos/<repo>/releases/latest
 *   - tag 与当前 versionName 比对
 *       一致  -> 返回 release note（公告内容）
 *       不一致 -> 标记有新版本，返回 notes + 下载直链
 *   拉取失败 -> 返回失败原因，由调用方提示「手动前往 GitHub」
 */
object Updater {
    const val REPO = "Devout9527/TexInject"
    private const val API = "https://api.github.com/repos/$REPO/releases/latest"
    val PAGE_URL = "https://github.com/$REPO/releases/latest"

    data class Result(
        val ok: Boolean,
        val hasUpdate: Boolean,
        val latestTag: String = "",
        val notes: String = "",
        val apkUrl: String = "",
        val error: String = ""
    )

    /** 当前 app 版本名（读 PackageManager，避免依赖 BuildConfig 字段名）。 */
    fun currentVersion(context: Context): String = try {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: "?"
    } catch (e: Throwable) {
        "?"
    }

    /** 版本号比较：a > b 返回 true。容忍 v 前缀与非数字段。 */
    private fun newer(a: String, b: String): Boolean {
        fun norm(s: String) = s.trim().removePrefix("v").removePrefix("V")
            .split('.', '-', '+').map { it.filter { ch -> ch.isDigit() } }.filter { it.isNotEmpty() }
            .map { it.toIntOrNull() ?: 0 }
        val x = norm(a); val y = norm(b)
        for (i in 0 until maxOf(x.size, y.size)) {
            val u = x.getOrElse(i) { 0 }; val v = y.getOrElse(i) { 0 }
            if (u != v) return u > v
        }
        return false
    }

    fun check(context: Context): Result {
        return try {
            val conn = (URL(API).openConnection() as HttpURLConnection).apply {
                connectTimeout = 8000
                readTimeout = 8000
                setRequestProperty("Accept", "application/vnd.github+json")
                setRequestProperty("User-Agent", "TexInject")
            }
            val code = conn.responseCode
            if (code !in 200..299) {
                return Result(false, false, error = "HTTP $code")
            }
            val text = BufferedReader(conn.inputStream.reader()).use { it.readText() }
            val j = JSONObject(text)
            val tag = j.optString("tag_name", "")
            val body = j.optString("body", "")
            var apk = ""
            j.optJSONArray("assets")?.let { arr ->
                for (i in 0 until arr.length()) {
                    val a = arr.optJSONObject(i) ?: continue
                    val name = a.optString("name", "")
                    if (name.endsWith(".apk", true)) {
                        apk = a.optString("browser_download_url", "")
                        break
                    }
                }
            }
            val cur = currentVersion(context)
            Result(
                ok = true,
                hasUpdate = tag.isNotEmpty() && newer(tag, cur),
                latestTag = tag,
                notes = body,
                apkUrl = apk
            )
        } catch (e: Throwable) {
            Result(false, false, error = "${e.javaClass.simpleName}: ${e.message}")
        }
    }
}
