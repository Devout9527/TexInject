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
package com.kael.texinject.ui

import android.content.Context
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/**
 * 快捷键存储：记录「哪些 MCP 模块要显示悬浮按钮」。
 * name = MCP 模块名；shown = 是否显示该模块的悬浮窗。
 * 存在 texinject_hotkeys 的 "list"（JSON 数组）。
 */
data class HotkeyItem(val name: String, val shown: Boolean)

object HotkeyStore {
    /**
     * 快捷键状态**跟着 MCP 走**：
     *   配置文件放在「加载的 MCP 同级目录」，例如
     *     mcp/1/2.mcp   ->  mcp/1/texinject.hotkeys.json
     *     mcp/2/1.mcp   ->  mcp/2/texinject.hotkeys.json
     * 这样不同目录下的 MCP 不会互相串配置。
     *
     * 没有加载任何 MCP 时（activeMcp == null）快捷键一律不显示。
     */
    private const val FILE_NAME = "texinject.hotkeys.json"

    @Volatile
    var activeMcp: File? = null

    /** 当前生效的配置文件；没有 MCP 时返回 null。 */
    private fun file(): File? {
        val m = activeMcp ?: return null
        val dir = m.parentFile ?: return null
        if (!dir.isDirectory) return null
        return File(dir, FILE_NAME)
    }

    /** 是否已经有 MCP 处于「已加载」状态。 */
    fun hasActive(): Boolean = activeMcp != null

    /** 读整个配置（没有文件就返回空对象）。 */
    private fun readAll(): JSONObject {
        val f = file() ?: return JSONObject()
        if (!f.isFile) return JSONObject()
        return try {
            JSONObject(f.readText())
        } catch (e: Throwable) {
            JSONObject()
        }
    }

    private fun writeAll(o: JSONObject) {
        val f = file() ?: return
        try {
            f.writeText(o.toString(2))
        } catch (e: Throwable) {
        }
    }

    fun load(c: Context): MutableList<HotkeyItem> {
        val out = mutableListOf<HotkeyItem>()
        val arr = readAll().optJSONArray("list") ?: return out
        return try {
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                out.add(HotkeyItem(o.optString("name"), o.optBoolean("shown", false)))
            }
            out
        } catch (e: Throwable) {
            out
        }
    }

    fun save(c: Context, list: List<HotkeyItem>) {
        val arr = JSONArray()
        for (it in list) {
            arr.put(JSONObject().apply {
                put("name", it.name)
                put("shown", it.shown)
            })
        }
        val o = readAll()
        try { o.put("list", arr) } catch (e: Throwable) {}
        writeAll(o)
    }

    // ---------- 几项功能开关状态（与快捷键同文件） ----------

    /** 读一个功能开关；没存过返回 def。 */
    fun feature(c: Context, name: String, def: Boolean = false): Boolean =
        readAll().optJSONObject("features")?.optBoolean(name, def) ?: def

    fun setFeature(c: Context, name: String, value: Boolean) {
        val o = readAll()
        val f = o.optJSONObject("features") ?: JSONObject().also {
            try { o.put("features", it) } catch (e: Throwable) {}
        }
        try { f.put(name, value) } catch (e: Throwable) {}
        writeAll(o)
    }

    fun isShown(c: Context, name: String): Boolean =
        load(c).firstOrNull { it.name == name }?.shown ?: false

    fun setShown(c: Context, name: String, shown: Boolean) {
        val list = load(c)
        val idx = list.indexOfFirst { it.name == name }
        if (idx >= 0) list[idx] = list[idx].copy(shown = shown)
        else list.add(HotkeyItem(name, shown))
        save(c, list)
    }

    /** 去掉已经不存在的模块（MCP 换了/删了）。 */
    fun prune(c: Context, validNames: Set<String>) {
        val list = load(c)
        val kept = list.filter { it.name in validNames }
        if (kept.size != list.size) save(c, kept)
    }

    /** 悬浮按钮位置（每个模块独立），没存过返回 null。 */
    fun pos(c: Context, name: String): Pair<Float, Float>? {
        val s = readAll().optJSONObject("pos")?.optString(name, "") ?: return null
        if (s.isEmpty()) return null
        val a = s.split(',')
        if (a.size != 2) return null
        val x = a[0].toFloatOrNull() ?: return null
        val y = a[1].toFloatOrNull() ?: return null
        return x to y
    }

    fun setPos(c: Context, name: String, x: Float, y: Float) {
        val o = readAll()
        val m = o.optJSONObject("pos") ?: JSONObject().also {
            try { o.put("pos", it) } catch (e: Throwable) {}
        }
        try { m.put(name, "$x,$y") } catch (e: Throwable) {}
        writeAll(o)
    }

    /** 快捷键分类里当前选中的分类（"" = 全部）。 */
    fun selectedCat(c: Context): String = readAll().optString("sel_cat", "")

    fun setSelectedCat(c: Context, value: String) {
        val o = readAll()
        try { o.put("sel_cat", value) } catch (e: Throwable) {}
        writeAll(o)
    }
}
