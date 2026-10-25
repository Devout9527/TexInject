package com.kael.texinject

import android.content.Context
import org.json.JSONObject
import java.io.BufferedReader
import java.net.HttpURLConnection
import java.net.URL

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

    fun currentVersion(context: Context): String = try {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: "?"
    } catch (e: Throwable) {
        "?"
    }

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
