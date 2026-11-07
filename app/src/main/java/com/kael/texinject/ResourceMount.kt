package com.kael.texinject

import android.content.Context
import com.kael.texinject.nativecore.NativeCore
import java.io.File

object ResourceMount {

    private const val SP = "texinject_mount"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_MODE = "mode"

    const val MODE_OFFICIAL = 1
    const val MODE_MIRROR = 2

    fun mode(context: Context): Int =
        if (context.getSharedPreferences(SP, Context.MODE_PRIVATE).getInt(KEY_MODE, MODE_OFFICIAL) == MODE_MIRROR)
            MODE_MIRROR else MODE_OFFICIAL

    fun setMode(context: Context, value: Int) {
        context.getSharedPreferences(SP, Context.MODE_PRIVATE).edit().putInt(KEY_MODE, value).apply()
    }

    fun storageDir(): File = GamePaths.extStorage("pack_netease/active").apply { mkdirs() }

    fun enabled(context: Context): Boolean =
        context.getSharedPreferences(SP, Context.MODE_PRIVATE).getBoolean(KEY_ENABLED, false)

    fun setEnabled(context: Context, on: Boolean) {
        context.getSharedPreferences(SP, Context.MODE_PRIVATE).edit()
            .putBoolean(KEY_ENABLED, on).apply()
    }

    fun aliasSuffix(context: Context): String? {
        val target = findTarget() ?: return null
        return suffixOf(context, target.dir)
    }

    private fun suffixOf(context: Context, dir: File): String? {
        val raw = runCatching { context.applicationInfo.dataDir }.getOrNull() ?: return null
        val canonical = runCatching { File(raw).canonicalPath }.getOrDefault(raw)
        val path = runCatching { dir.canonicalPath }.getOrElse { dir.absolutePath }
        for (base in arrayOf(canonical, raw)) {
            if (path.startsWith("$base/")) return path.removePrefix(base)
        }
        return null
    }

    private fun findTarget(): PackInjector.Target? {
        runCatching { PackInjector.findTarget() }.getOrNull()?.let { return it }
        val root = File(GamePaths.filesDir, "games/com.netease/resource_packs")
        val candidates = root.listFiles { f -> f.isDirectory && f.name.contains("FirstPatch", true) }
            ?: return null
        val best = candidates.filter { File(it, "textures").isDirectory }
            .maxByOrNull { it.lastModified() }
            ?: candidates.maxByOrNull { it.lastModified() }
            ?: return null
        return PackInjector.Target(best, "3.9")
    }

    fun configure(context: Context): Boolean = try {
        val dataDir = context.applicationInfo.dataDir
        val alias = aliasSuffix(context) ?: return false
        val canonical = runCatching { File(dataDir).canonicalPath }.getOrDefault(dataDir)
        NativeCore.mountConfigure(dataDir, canonical, alias, storageDir().canonicalPath)
    } catch (t: Throwable) {
        false
    }

    fun ensure(context: Context): Boolean {
        if (!configure(context)) {
            setEnabled(context, false)
            return false
        }
        val mounted = try {
            NativeCore.mountInstall() && NativeCore.mountStatus() == "mounted"
        } catch (t: Throwable) {
            false
        }
        setEnabled(context, mounted)
        return mounted
    }

    fun clear(context: Context): Int {
        var removed = 0
        val root = storageDir()
        root.walkBottomUp().forEach { f ->
            if (f != root && f.isFile && f.delete()) removed++
        }
        setEnabled(context, false)
        return removed
    }

    fun status(): String = try {
        when (val s = NativeCore.mountStatus()) {
            "mounted" -> "已生效"
            "waiting_for_file_imports" -> "等待游戏加载"
            "not_configured" -> "未配置"
            else -> s
        }
    } catch (t: Throwable) {
        "不可用：${t.message ?: t.javaClass.simpleName}"
    }
}
