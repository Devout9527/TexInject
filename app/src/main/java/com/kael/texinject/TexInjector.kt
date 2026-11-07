package com.kael.texinject

import android.content.Context
import com.kael.texinject.ui.compose.DevMode
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
            if (mode == 1) {

                DevMode.ensure(context)
                val choice = if (DevMode.enabled) ResourceMount.mode(context)
                             else ResourceMount.MODE_OFFICIAL
                val mirrored = if (choice == ResourceMount.MODE_MIRROR) {
                    if (!ResourceMount.ensure(context)) {
                        return Result(
                            false,
                            "镜像目录装不上（找不到目标包或导入不全），已取消注入；" +
                                "改成「官方包」再试",
                            listOf("提示：镜像需要先找到游戏正在加载的资源包目录"),
                        )
                    }
                    true
                } else false
                val r = if (mirrored && packs.isNotEmpty()) {
                    PackInjector.injectToMirror(source, packs, ResourceMount.storageDir())
                } else {

                    if (choice == ResourceMount.MODE_OFFICIAL) ResourceMount.setEnabled(context, false)
                    PackInjector.inject(context, source, packs)
                }
                Result(r.ok, r.summary, r.lines)
            } else {

                val removed = ResourceMount.clear(context)
                val r = PackInjector.restore(context)
                val summary = if (removed > 0) {
                    "已清空映射目录（$removed 个文件）／${r.summary}"
                } else r.summary
                Result(r.ok || removed > 0, summary, r.lines)
            }
        } catch (e: Throwable) {
            Result(false, "$verb 失败：${e.message}", listOf("ERR ${e.message}"))
        }
    }

    fun targetHasContent(): Boolean = PackInjector.targetHasContent()

    fun backupExists(): Boolean = PackInjector.backupExists()
}
