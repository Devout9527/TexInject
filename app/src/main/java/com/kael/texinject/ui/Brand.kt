package com.kael.texinject.ui

import android.content.Context

object Brand {
    private const val PREF = "texinject_brand"
    private const val KEY_QUOTE = "quote"

    const val DEFAULT_QUOTE = "路漫漫其修远兮，吾将上下而求索"

    fun quote(c: Context): String =
        c.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .getString(KEY_QUOTE, DEFAULT_QUOTE) ?: DEFAULT_QUOTE

    fun setQuote(c: Context, v: String) {
        c.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit().putString(KEY_QUOTE, v).apply()
    }

    fun title(c: Context): String {
        val q = quote(c).trim()
        return if (q.isEmpty()) "TexInject" else "TexInject · $q"
    }
}
