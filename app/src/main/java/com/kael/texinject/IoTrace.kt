package com.kael.texinject

import android.content.Context
import com.kael.texinject.nativecore.NativeCore
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object IoTrace {

    const val KIND_OPENAT = 0
    const val KIND_OPEN = 1
    const val KIND_FOPEN = 2
    const val KIND_STAT = 3
    const val KIND_LSTAT = 4
    const val KIND_ACCESS = 5
    const val KIND_FACCESSAT = 6
    const val KIND_OPENDIR = 7
    const val KIND_MKDIR = 8
    const val KIND_UNLINK = 9
    const val KIND_RENAME = 10
    const val KIND_RMDIR = 11
    const val KIND_TRUNCATE = 12

    const val TRUNC_FLAG = 0x8000

    private val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US)

    fun dir(context: Context): File =
        File(context.filesDir, "TexInject/trace").apply { runCatching { mkdirs() } }

    fun start(): String = try {
        val imports = NativeCore.pltInit()
        if (imports <= 0) {
            "PLT 初始化失败：${NativeCore.pltLastError()}"
        } else if (!NativeCore.ioTraceStart()) {
            "启动失败：${NativeCore.ioTraceLastError()}"
        } else {
            "已开始记录（导入符号 $imports 个）"
        }
    } catch (t: Throwable) {
        "不可用：${t.message ?: t.javaClass.simpleName}"
    }

    fun stop(): String = try {
        NativeCore.ioTraceStop()
        "已停止（本轮共 ${count()} 条）"
    } catch (t: Throwable) {
        "不可用：${t.message ?: t.javaClass.simpleName}"
    }

    fun running(): Boolean = try {
        NativeCore.ioTraceRunning()
    } catch (t: Throwable) {
        false
    }

    fun count(): Int = try {
        NativeCore.ioTraceCount()
    } catch (t: Throwable) {
        0
    }

    fun dump(context: Context): File? = try {
        val f = File(dir(context), "io-${stamp.format(Date())}.txt")
        if (NativeCore.ioTraceDump(f.absolutePath)) f else null
    } catch (t: Throwable) {
        null
    }

    fun filter(src: File, keyword: String): List<String> = runCatching {
        src.readLines().filter { it.contains(keyword, ignoreCase = true) }
    }.getOrDefault(emptyList())
}
