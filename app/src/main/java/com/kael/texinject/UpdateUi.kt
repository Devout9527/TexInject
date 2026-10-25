package com.kael.texinject

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper

object UpdateUi {
    private const val PREF = "texinject_update"
    private const val KEY_SEEN = "seen_notes"

    private val main = Handler(Looper.getMainLooper())

    private fun prefs(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    private fun openUrl(c: Context, url: String) {
        try {
            c.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            })
        } catch (e: Throwable) {
        }
    }

    fun checkAndShow(context: Context, silent: Boolean) {
        val ui = (context as? android.app.Activity) ?: return
        val app = ui.applicationContext
        Thread({
            val r = Updater.check(app)
            main.post { show(ui, r, silent) }
        }, "texinject-update").start()
    }

    fun show(ui: android.app.Activity, r: Updater.Result, silent: Boolean) {
        val ctx = ui
        try {
            if (!r.ok) {
                if (silent) return
                AlertDialog.Builder(ctx)
                    .setTitle("检查更新失败")
                    .setMessage("${r.error}\n\n可手动前往 GitHub 更新：\n${Updater.PAGE_URL}")
                    .setPositiveButton("前往 GitHub") { _, _ -> openUrl(ctx, Updater.PAGE_URL) }
                    .setNegativeButton("关闭", null)
                    .show()
                return
            }
            if (r.hasUpdate) {
                AlertDialog.Builder(ctx)
                    .setTitle("发现新版本 ${r.latestTag}（当前 ${Updater.currentVersion(ctx)}）")
                    .setMessage(r.notes.ifBlank { "（无更新说明）" }.take(2000))
                    .setPositiveButton("前往下载") { _, _ ->
                        openUrl(ctx, r.apkUrl.ifBlank { Updater.PAGE_URL })
                    }
                    .setNegativeButton("关闭", null)
                    .show()
                return
            }
            val tag = r.latestTag
            if (silent && prefs(ctx).getString(KEY_SEEN, "") == tag) return
            AlertDialog.Builder(ctx)
                .setTitle("更新内容 ${tag}")
                .setMessage(r.notes.ifBlank { "当前已是最新版本。" }.take(2000))
                .setPositiveButton("不再显示") { _, _ ->
                    prefs(ctx).edit().putString(KEY_SEEN, tag).apply()
                }
                .setNegativeButton("关闭", null)
                .show()
        } catch (e: Throwable) {
        }
    }

    fun maybeAskStar(context: Context) {
        val ctx = (context as? android.app.Activity) ?: return
        try {
            if (!LaunchCounter.shouldAskStar(ctx)) return
            LaunchCounter.markStarShown(ctx)
            AlertDialog.Builder(ctx)
                .setTitle("已启动 ${LaunchCounter.count(ctx)} 次")
                .setMessage("你已经启动 100 次了，点个 star 支持一下吗 ʚ♡⃛ɞ(ू•ᴗ•ू❁)")
                .setPositiveButton("去点 star") { _, _ -> openUrl(ctx, "https://github.com/${Updater.REPO}") }
                .setNegativeButton("关闭", null)
                .show()
        } catch (e: Throwable) {
        }
    }
}
