package com.kael.texinject.log

import android.content.Context
import android.os.Build
import com.kael.texinject.GamePaths
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

object TxLog {

    private const val DIR = "pack_netease/logs"
    private const val MAX_BYTES = 8L * 1024 * 1024
    private const val KEEP_FILES = 20

    private val bootAt: Date = Date()
    private val bootStamp: String =
        SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(bootAt)

    private val timeFmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    private val dateFmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)

    private val io = Executors.newSingleThreadExecutor { r ->
        Thread(r, "tx-log").apply { isDaemon = true }
    }

    @Volatile private var file: File? = null
    @Volatile private var enabled = true
    private val headerDone = AtomicBoolean(false)

    fun init(ctx: Context?, extra: String = "") {
        if (!headerDone.compareAndSet(false, true)) return
        val f = ensureFile(ctx)
        if (f == null) {
            enabled = false
            return
        }
        io.execute {
            try {
                f.appendText(buildHeader(ctx, extra))
            } catch (_: Throwable) {
            }
        }
        pruneOld(f.parentFile)
    }

    fun file(): File? = file

    fun dir(ctx: Context): File? {
        val base = ctx.getExternalFilesDir(null) ?: return null
        return File(base, DIR)
    }

    private fun ensureFile(ctx: Context?): File? {
        file?.let { return it }
        if (ctx == null) return null
        return try {
            val d = dir(ctx) ?: return null
            if (!d.isDirectory && !d.mkdirs()) return null
            val f = File(d, "$bootStamp.log")
            file = f
            f
        } catch (_: Throwable) {
            null
        }
    }

    private fun pruneOld(d: File?) {
        if (d == null) return
        io.execute {
            try {
                val all = d.listFiles { x -> x.isFile && x.name.endsWith(".log") }
                    ?.sortedByDescending { it.name } ?: return@execute
                all.drop(KEEP_FILES).forEach { runCatching { it.delete() } }
            } catch (_: Throwable) {
            }
        }
    }

    private fun buildHeader(ctx: Context?, extra: String): String {
        val sb = StringBuilder()
        val line = "=".repeat(64)
        sb.append(line).append('\n')
        sb.append("TexInject 运行日志\n")
        sb.append(line).append('\n')
        sb.append("[启动时间] ").append(dateFmt.format(bootAt)).append('\n')
        sb.append("[日志文件] ").append(file?.absolutePath ?: "(无)").append('\n')
        sb.append('\n')

        sb.append("--- 系统环境 ---\n")
        sb.append("机型       : ").append(Build.MANUFACTURER).append(' ')
            .append(Build.MODEL).append(" (").append(Build.DEVICE).append(")\n")
        sb.append("Android    : ").append(Build.VERSION.RELEASE)
            .append(" (API ").append(Build.VERSION.SDK_INT).append(")\n")
        sb.append("安全补丁   : ").append(Build.VERSION.SECURITY_PATCH ?: "-").append('\n')
        sb.append("Build ID   : ").append(Build.ID).append('\n')
        sb.append("ABI        : ").append(Build.SUPPORTED_ABIS.joinToString(", ")).append('\n')
        sb.append("CPU 核心   : ").append(Runtime.getRuntime().availableProcessors()).append('\n')
        sb.append("总内存     : ").append(mb(Runtime.getRuntime().maxMemory())).append('\n')
        sb.append('\n')

        sb.append("--- 宿主 ---\n")
        val pkg = GamePaths.pkg
        sb.append("包名       : ").append(pkg.ifEmpty { "(未知)" }).append('\n')
        sb.append("游戏版本   : ").append(GamePaths.version.ifEmpty { "(未知)" }).append('\n')
        val hostVer = try {
            ctx?.packageManager?.getPackageInfo(pkg, 0)?.versionName ?: "-"
        } catch (_: Throwable) {
            "-"
        }
        val hostCode = try {
            if (Build.VERSION.SDK_INT >= 28) {
                ctx?.packageManager?.getPackageInfo(pkg, 0)?.longVersionCode?.toString() ?: "-"
            } else "-"
        } catch (_: Throwable) {
            "-"
        }
        sb.append("宿主版本   : ").append(hostVer).append(" (").append(hostCode).append(")\n")
        sb.append("进程       : ").append(currentProcessName()).append('\n')
        sb.append('\n')

        sb.append("--- 模块 ---\n")
        sb.append("模块版本   : ").append(moduleVersion(ctx)).append('\n')
        sb.append("日志级别   : I（信息）\n")
        if (extra.isNotEmpty()) sb.append("附加       : ").append(extra).append('\n')
        sb.append(line).append('\n')
        sb.append('\n')
        return sb.toString()
    }

    private fun mb(v: Long): String = "%.0f MB".format(v / 1024.0 / 1024.0)

    private fun moduleVersion(ctx: Context?): String {

        val fromBuild = runCatching {
            com.kael.texinject.BuildConfig::class.java
                .getField("VERSION_NAME").get(null) as? String
        }.getOrNull()
        if (!fromBuild.isNullOrEmpty()) return fromBuild

        return try {
            ctx?.createPackageContext("com.kael.texinject", 0)
                ?.packageManager?.getPackageInfo("com.kael.texinject", 0)?.versionName
                ?: "(未知)"
        } catch (_: Throwable) {
            "(未知)"
        }
    }

    private fun currentProcessName(): String {
        return try {
            val f = File("/proc/self/cmdline")
            if (f.isFile) f.readText().trim().trimEnd('\u0000') else "-"
        } catch (_: Throwable) {
            "-"
        }
    }

    fun d(tag: String, msg: String) = write('D', tag, msg)
    fun i(tag: String, msg: String) = write('I', tag, msg)
    fun w(tag: String, msg: String) = write('W', tag, msg)

    fun e(tag: String, msg: String, t: Throwable? = null) {
        if (t == null) {
            write('E', tag, msg)
            return
        }
        val sw = StringWriter()
        t.printStackTrace(PrintWriter(sw))
        write('E', tag, "$msg\n$sw")
    }

    private fun write(level: Char, tag: String, msg: String) {
        if (!enabled) return
        val f = file ?: return
        val ts = timeFmt.format(Date())
        val line = "$ts $level/$tag: $msg"
        io.execute {
            try {
                if (f.length() > MAX_BYTES) return@execute
                f.appendText(line + "\n")
            } catch (_: Throwable) {
            }
        }
    }

    fun flushNow(level: Char, tag: String, msg: String) {
        if (!enabled) return
        val f = file ?: return
        try {
            val ts = timeFmt.format(Date())
            if (f.length() <= MAX_BYTES) f.appendText("$ts $level/$tag: $msg\n")
        } catch (_: Throwable) {
        }
    }

    fun feature(name: String, on: Boolean, detail: String = "") {
        val s = if (on) "开" else "关"
        i("Feature", "$name = $s" + if (detail.isNotEmpty()) "  ($detail)" else "")
    }

    fun material(name: String, md5: String, action: String = "注入") {
        i("Material", "$action  $name  md5=$md5")
    }

    fun touch(what: String, point: IntArray?, region: android.graphics.Rect?, passed: Boolean) {
        val p = if (point != null) "(${point[0]},${point[1]})" else "(?)"
        val r = if (region != null) "[${region.left},${region.top},${region.right},${region.bottom}]" else "[空]"
        val v = if (passed) "穿透->游戏" else "模块接管"
        i("Touch", "$what 点=$p 区域=$r -> $v")
    }

    fun section(title: String) {
        i("----", "===== $title =====")
    }
}
