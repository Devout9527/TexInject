/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 扩展功能的快捷键。
 *
 * 原来的「快捷键」分类是给 Python/MCP 模块用的，按键由脚本侧自己读配置，
 * Java 层一个 KeyEvent 都没拦过。扩展页这些功能（Cookie 登录、NBT 编辑器）
 * 是纯 Kotlin 的，所以要在这里自己建一套：动作表 + 绑定存储 + dispatchKeyEvent 拦截。
 */
package com.kael.texinject

import android.app.Activity
import android.content.Context
import android.view.KeyEvent

import com.kael.texinject.ui.FeatureHotkeys

object ExtHotkeys {

    private const val SP = "texinject_ext_hotkey"

    /** 一个可以被快捷键唤出的扩展功能。 */
    class Action(
        val id: String,
        val name: String,
        /** 点悬浮开关时执行：第二个参数是切换后的状态。 */
        val onToggle: (Activity, Boolean) -> Unit,
    )

    private val actions = LinkedHashMap<String, Action>()

    /** 每个功能的开关状态，悬浮按钮和窗口共用。 */
    private val state = java.util.concurrent.ConcurrentHashMap<String, Boolean>()

    /**
     * 允许绑定的键。
     *
     * 不做「按键录制」：录制要在对话框里先吃掉一次按键，跟游戏输入抢事件很容易
     * 搞成绑不上还退不出来。给一份固定的候选，选中即生效，够用且不会出错。
     */
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

    /**
     * 功能总开关。和「悬浮」是两件事：
     *   - 这里关掉 → 快捷键不响应、点「悬浮」也不弹，等于这个扩展整个停用
     *   - 「悬浮」只负责把悬浮窗摆出来
     * 默认关：用户自己开，避免一按快捷键就冒出一个窗口。
     */
    fun isEnabled(c: Context, id: String): Boolean = prefs(c).getBoolean("on_$id", false)

    fun setEnabled(c: Context, id: String, on: Boolean) {
        prefs(c).edit().putBoolean("on_$id", on).apply()
        if (!on) FeatureHotkeys.hide(id)
    }

    /**
     * 按键分发。命中就把快捷键对应的悬浮开关摆到屏幕上（不是直接执行），
     * 开关再点一下才真正开/关 ——— 跟「快捷键」页里那些功能的行为保持一致。
     */
    fun handle(activity: Activity, event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount > 0) {
            return false
        }
        val code = event.keyCode
        for (action in actions.values) {
            if (keyOf(activity, action.id) != code) {
                continue
            }
            // 功能总开关关着就完全不响应
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

    /** 把某个扩展功能的悬浮开关显示出来（重复调用只是刷新）。 */
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

    /** 面板里点「悬浮」按钮也走这里，保证状态一致。 */
    fun showWidget(activity: Activity, id: String) {
        actions[id]?.let { attachWidget(activity, it) }
    }
}
