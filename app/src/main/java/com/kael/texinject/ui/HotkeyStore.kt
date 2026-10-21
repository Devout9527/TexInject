package com.kael.texinject.ui

import android.content.Context
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

data class HotkeyItem(val name: String, val shown: Boolean)

object HotkeyStore {
    private const val FILE_NAME = "texinject.hotkeys.json"

    @Volatile
    var activeMcp: File? = null

    private fun file(): File? {
        val m = activeMcp ?: return null
        val dir = m.parentFile ?: return null
        if (!dir.isDirectory) return null
        return File(dir, FILE_NAME)
    }

    fun hasActive(): Boolean = activeMcp != null

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

    fun prune(c: Context, validNames: Set<String>) {
        val list = load(c)
        val kept = list.filter { it.name in validNames }
        if (kept.size != list.size) save(c, kept)
    }

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

    fun selectedCat(c: Context): String = readAll().optString("sel_cat", "")

    fun setSelectedCat(c: Context, value: String) {
        val o = readAll()
        try { o.put("sel_cat", value) } catch (e: Throwable) {}
        writeAll(o)
    }
}
