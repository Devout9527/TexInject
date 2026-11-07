package com.kael.texinject

import android.content.Context

object CookiePrefs {

    private const val SP = "texinject_cookie"
    private const val K_ENABLED = "enabled"
    private const val K_AUTO = "autoLogin"
    private const val K_SELECTED = "selected"

    private fun sp(ctx: Context) = ctx.getSharedPreferences(SP, Context.MODE_PRIVATE)

    fun enabled(ctx: Context): Boolean = sp(ctx).getBoolean(K_ENABLED, true)

    fun setEnabled(ctx: Context, v: Boolean) {
        sp(ctx).edit().putBoolean(K_ENABLED, v).apply()
    }

    fun autoLogin(ctx: Context): Boolean = sp(ctx).getBoolean(K_AUTO, true)

    fun setAutoLogin(ctx: Context, v: Boolean) {
        sp(ctx).edit().putBoolean(K_AUTO, v).apply()
    }

    fun selected(ctx: Context): String = sp(ctx).getString(K_SELECTED, "") ?: ""

    fun setSelected(ctx: Context, name: String) {
        sp(ctx).edit().putString(K_SELECTED, name).apply()
    }

    fun selectedData(ctx: Context): String? {
        val n = selected(ctx)
        if (n.isEmpty()) return null
        return CookieStore.read(ctx, n)
    }
}
