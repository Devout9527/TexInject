package com.kael.texinject.ui

import android.content.Context
import android.content.res.Configuration
import android.graphics.Color

object UiTheme {
    val accentBlue = Color.parseColor("#0A84FF")
    val accentBlueDark = Color.parseColor("#0066CC")
    val red = Color.parseColor("#FF3B30")

    val switchOn = Color.parseColor("#0A84FF")
    val switchOff = Color.parseColor("#E5E7EB")
    val switchThumbShadow = Color.parseColor("#33000000")

    fun isDarkMode(context: Context): Boolean {
        val mode = context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        return mode == Configuration.UI_MODE_NIGHT_YES
    }

    val cbPanel = Color.parseColor("#C0252525")
    val cbList = Color.parseColor("#C0121212")
    val cbHighlight = Color.parseColor("#FF0062FF")
    val cbDivider = Color.parseColor("#A9A9A9")
    val cbCard = Color.parseColor("#D0121212")

    val glassWhite = Color.parseColor("#59FFFFFF")
    val glassWhiteStrong = Color.parseColor("#73FFFFFF")
    val glassWhiteBorder = Color.parseColor("#66FFFFFF")
    val glassWhiteText = Color.parseColor("#FF141414")

    fun glassCard(dark: Boolean) = Color.parseColor(if (dark) "#B31E1E22" else "#B3F9FAFC")
    fun glassInner(dark: Boolean) = Color.parseColor(if (dark) "#732A2A2E" else "#73FFFFFF")
    fun glassButton(dark: Boolean) = Color.parseColor(if (dark) "#33FFFFFF" else "#26000000")
    fun glassButtonStroke(dark: Boolean) = Color.parseColor(if (dark) "#40FFFFFF" else "#33000000")
    val glassMuted = Color.parseColor("#59FFFFFF")
    val dim = Color.parseColor("#59000000")

    fun windowBg(dark: Boolean) = Color.parseColor(if (dark) "#F2121214" else "#E6000000")
    fun cardBg(dark: Boolean) = Color.parseColor(if (dark) "#1E1E22" else "#F9FAFC")
    fun innerCardBg(dark: Boolean) = Color.parseColor(if (dark) "#2A2A2E" else "#FFFFFF")
    fun sidebarBg(dark: Boolean) = Color.parseColor(if (dark) "#252529" else "#FFFFFF")
    fun divider(dark: Boolean) = Color.parseColor(if (dark) "#36363B" else "#E5E9F0")
    fun textPrimary(dark: Boolean) = Color.parseColor(if (dark) "#FFFFFF" else "#1C1C1E")
    fun textSecondary(dark: Boolean) = Color.parseColor(if (dark) "#9E9EA4" else "#8E8E93")
    fun inputBg(dark: Boolean) = Color.parseColor(if (dark) "#2A2A2E" else "#FFFFFF")
    fun inputBorder(dark: Boolean) = Color.parseColor(if (dark) "#3E3E44" else "#E5E7EB")
    fun exitBtnBg(dark: Boolean) = Color.parseColor(if (dark) "#3B1D1D" else "#FDE8E8")
    fun activeTabBg(dark: Boolean) = Color.parseColor(if (dark) "#1A3860" else "#EBF5FF")
}
