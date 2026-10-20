/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cookie 登录的开关状态。真正的 cookie 数据在 CookieStore（cookies 目录里的文件）。
 */
package com.kael.texinject

import android.content.Context

object CookiePrefs {

    private const val SP = "texinject_cookie"
    private const val K_ENABLED = "enabled"
    private const val K_AUTO = "autoLogin"
    private const val K_SELECTED = "selected"

    private fun sp(ctx: Context) = ctx.getSharedPreferences(SP, Context.MODE_PRIVATE)

    /** 总开关：开着才在登录流程里回填 cookie。默认开，真正的开关在 Cookie 页里。 */
    fun enabled(ctx: Context): Boolean = sp(ctx).getBoolean(K_ENABLED, true)

    fun setEnabled(ctx: Context, v: Boolean) {
        sp(ctx).edit().putBoolean(K_ENABLED, v).apply()
    }

    /** 自动登录：登录页出现时直接放行，不需要用户点。 */
    fun autoLogin(ctx: Context): Boolean = sp(ctx).getBoolean(K_AUTO, true)

    fun setAutoLogin(ctx: Context, v: Boolean) {
        sp(ctx).edit().putBoolean(K_AUTO, v).apply()
    }

    /** 当前选中的 cookie 文件名。 */
    fun selected(ctx: Context): String = sp(ctx).getString(K_SELECTED, "") ?: ""

    fun setSelected(ctx: Context, name: String) {
        sp(ctx).edit().putString(K_SELECTED, name).apply()
    }

    /** 取选中那条 cookie 的正文；没选中或文件没了返回 null。 */
    fun selectedData(ctx: Context): String? {
        val n = selected(ctx)
        if (n.isEmpty()) return null
        return CookieStore.read(ctx, n)
    }
}
