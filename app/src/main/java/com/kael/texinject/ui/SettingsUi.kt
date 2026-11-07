package com.kael.texinject.ui

import android.app.Activity
import android.view.View
import com.kael.texinject.ui.compose.ComposeHostView
import com.kael.texinject.ui.compose.SettingsScreen

object SettingsUi {
    fun buildView(activity: Activity): View =
        ComposeHostView(activity) { SettingsScreen(activity) }
}
