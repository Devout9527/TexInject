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

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper

/**
 * 更新 / 公告 的界面。
 *
 *  版本一致   -> 显示「更新内容」，两个选项：不再显示 / 关闭
 *  版本不一致 -> 显示「发现新版本」，可前往 GitHub（有直链就直接走直链）
 *  拉取失败   -> 提示手动前往 GitHub
 */
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

    /** 在后台检查，然后回主线程弹框。silent=true 时（自动检查）无更新就不打扰。 */
    fun checkAndShow(context: Context, silent: Boolean) {
        val ctx = context.applicationContext
        Thread({
            val r = Updater.check(ctx)
            main.post { show(ctx, r, silent) }
        }, "texinject-update").start()
    }

    fun show(ctx: Context, r: Updater.Result, silent: Boolean) {
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
            // 版本一致 -> 公告：更新内容
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

    /** 启动满 100 次提示点 star（只弹一次）。 */
    fun maybeAskStar(context: Context) {
        val ctx = context.applicationContext
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
