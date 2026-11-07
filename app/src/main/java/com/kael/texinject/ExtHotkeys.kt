package com.kael.texinject

import android.app.Activity
import android.content.Context
import android.view.KeyEvent

import com.kael.texinject.ui.FeatureHotkeys

object ExtHotkeys {

    private const val SP = "texinject_ext_hotkey"

    class Action(
        val id: String,
        val name: String,

        val onToggle: (Activity, Boolean) -> Unit,
    )

    private val actions = LinkedHashMap<String, Action>()

    private val state = java.util.concurrent.ConcurrentHashMap<String, Boolean>()

    val CANDIDATES: List<Pair<Int, String>> = listOf(
        KeyEvent.KEYCODE_VOLUME_UP to "音量+",
        KeyEvent.KEYCODE_VOLUME_DOWN to "音量-",
        KeyEvent.KEYCODE_HEADSETHOOK to "耳机键",
        KeyEvent.KEYCODE_CAMERA to "相机键",
        KeyEvent.KEYCODE_MENU to "菜单键",
        KeyEvent.KEYCODE_BACK to "返回键",
        KeyEvent.KEYCODE_F1 to "F1",
        KeyEvent.KEYCODE_F2 to "F2",
        KeyEvent.KEYCODE_F3 to "F3",
        KeyEvent.KEYCODE_F4 to "F4",
    )

    fun register(action: Action) {
        actions[action.id] = action
    }

    fun all(): List<Action> = actions.values.toList()

    fun find(id: String): Action? = actions[id]

    private fun prefs(c: Context) = c.getSharedPreferences(SP, Context.MODE_PRIVATE)

    fun keyOf(c: Context, id: String): Int = prefs(c).getInt(id, 0)

    fun keyName(keyCode: Int): String =
        CANDIDATES.firstOrNull { it.first == keyCode }?.second ?: "未绑定"

    fun bind(c: Context, id: String, keyCode: Int) {
        prefs(c).edit().putInt(id, keyCode).apply()
    }

    fun unbind(c: Context, id: String) {
        prefs(c).edit().remove(id).apply()
    }

    fun isEnabled(c: Context, id: String): Boolean = prefs(c).getBoolean("on_$id", false)

    fun setEnabled(c: Context, id: String, on: Boolean) {
        prefs(c).edit().putBoolean("on_$id", on).apply()
        if (!on) FeatureHotkeys.hide(id)
    }

    fun handle(activity: Activity, event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount > 0) {
            return false
        }
        val code = event.keyCode
        for (action in actions.values) {
            if (keyOf(activity, action.id) != code) {
                continue
            }

            if (!isEnabled(activity, action.id)) {
                continue
            }
            return runCatching {
                attachWidget(activity, action)
                true
            }.getOrDefault(false)
        }
        return false
    }

    fun attachWidget(activity: Activity, action: Action) {
        if (!isEnabled(activity, action.id)) {
            return
        }
        FeatureHotkeys.init(activity)
        val isOn = state[action.id] ?: false
        FeatureHotkeys.show(action.id, action.name, isOn) {
            val next = !(state[action.id] ?: false)
            state[action.id] = next
            FeatureHotkeys.setOn(action.id, next)
            runCatching { action.onToggle(activity, next) }
        }
    }

    fun showWidget(activity: Activity, id: String) {
        actions[id]?.let { attachWidget(activity, it) }
    }
}
