package com.kael.texinject

import android.content.Context

object LaunchCounter {
    private const val PREF = "texinject_launch"
    private const val KEY_COUNT = "count"
    private const val KEY_STAR_SHOWN = "star_shown"

    const val STAR_THRESHOLD = 100

    private fun p(c: Context) = c.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun bump(c: Context): Int {
        val n = p(c).getInt(KEY_COUNT, 0) + 1
        p(c).edit().putInt(KEY_COUNT, n).apply()
        return n
    }

    fun count(c: Context): Int = p(c).getInt(KEY_COUNT, 0)

    fun starShown(c: Context): Boolean = p(c).getBoolean(KEY_STAR_SHOWN, false)

    fun markStarShown(c: Context) {
        p(c).edit().putBoolean(KEY_STAR_SHOWN, true).apply()
    }

    fun shouldAskStar(c: Context): Boolean = count(c) >= STAR_THRESHOLD && !starShown(c)
}
