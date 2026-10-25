package com.kael.texinject

import android.content.Context
import java.io.File

object TexInjector {

    data class Result(val ok: Boolean, val summary: String, val detail: List<String>)

    fun scanPacks(source: File): List<File> {
        if (!source.isDirectory) return emptyList()
        return source.listFiles { f ->
            f.isFile && (f.name.endsWith(".mcpack", true) || f.name.endsWith(".zip", true))
        }?.sortedBy { it.name } ?: emptyList()
    }

    fun injectAll(context: Context, source: File): Result {
        val packs = scanPacks(source)
        if (packs.isEmpty()) {
            return Result(false, "源目录无材质包：${source.absolutePath}", emptyList())
        }
        return run(context, source, packs, mode = 1, verb = "注入")
    }

    fun inject(context: Context, source: File, file: File): Result {
        if (!file.isFile) return Result(false, "文件不存在：${file.name}", emptyList())
        return run(context, source, listOf(file), mode = 1, verb = "注入")
    }

    fun restore(context: Context, source: File): Result =
        run(context, source, scanPacks(source), mode = 0, verb = "还原")

    private fun run(
        context: Context, source: File, packs: List<File>, mode: Int, verb: String
    ): Result {
        return try {
            GamePaths.init(context)
            val r = if (mode == 1) {
                PackInjector.inject(context, source, packs)
            } else {
                PackInjector.restore(context)
            }
            Result(r.ok, r.summary, r.lines)
        } catch (e: Throwable) {
            Result(false, "$verb 失败：${e.message}", listOf("ERR ${e.message}"))
        }
    }

    fun targetHasContent(): Boolean = PackInjector.targetHasContent()

    fun backupExists(): Boolean = PackInjector.backupExists()
}
