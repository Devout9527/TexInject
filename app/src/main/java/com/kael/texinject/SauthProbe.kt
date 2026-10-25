package com.kael.texinject

import android.content.Context
import android.util.Log
import io.github.libxposed.api.XposedInterface
import io.github.libxposed.api.XposedModule
import java.io.File
import java.text.SimpleDateFormat
import java.util.Collections
import java.util.Date
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

object SauthProbe {

    private const val TAG = "TexInject-Sauth"

    private const val TARGET = "com.netease.ntunisdk.base.SdkBase"

    private const val ESCAPE = "/data/local/tmp/texinject_no_probe"

    private val WHITELIST = setOf(
        "getJFSauthJson",
        "setBackSauthLoginJson",
        "setLoginSauthInfo",
        "setJFSauth",
        "setJFSauthWithKey",
        "getJfSauthChannelMap"
    )

    private const val MAX_VALUE = 240

    private val hooked = Collections.synchronizedSet(HashSet<String>())

    @Volatile
    private var logFile: File? = null

    @Volatile
    private var appCtx: Context? = null

    @Volatile
    private var moduleRef: XposedModule? = null

    @Volatile
    private var installed = false

    @Volatile
    var hookedClass: Boolean = false
        private set

    private val dumped = Collections.synchronizedSet(HashSet<String>())

    @Volatile
    var armCapture: Boolean = false

    private val io: ExecutorService by lazy {
        Executors.newSingleThreadExecutor { r ->
            Thread(r, "texinject-probe-io").apply { isDaemon = true }
        }
    }

    private val timeFmt = object : ThreadLocal<SimpleDateFormat>() {
        override fun initialValue(): SimpleDateFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    }

    fun init(ctx: Context) {
        appCtx = ctx.applicationContext ?: ctx
        try {
            CookieStore.dir(ctx)
        } catch (t: Throwable) {
            Log.e(TAG, "ensure cookies dir fail", t)
        }
        if (logFile != null) return
        try {
            val base = ctx.getExternalFilesDir(null) ?: ctx.filesDir
            val dir = File(base, "pack_netease")
            if (!dir.isDirectory) dir.mkdirs()
            val f = File(dir, "sauth_probe.log")
            logFile = f
            f.writeText("=== TexInject sauth probe ${Date()} ===\n")
            Log.i(TAG, "log -> " + f.absolutePath)
        } catch (t: Throwable) {
            Log.e(TAG, "init log fail", t)
        }
    }

    fun install(module: XposedModule) {
        moduleRef = module
        if (installed) return
        installed = true
        if (File(ESCAPE).exists()) {
            log("escape file present, probe off")
            return
        }
        hookLoader("java.lang.ClassLoader", "loadClass", String::class.java, java.lang.Boolean.TYPE).invoke(module)
        hookLoader("dalvik.system.BaseDexClassLoader", "loadClass", String::class.java, java.lang.Boolean.TYPE).invoke(module)
        hookLoader("dalvik.system.BaseDexClassLoader", "loadClass", String::class.java).invoke(module)
        hookLoader("dalvik.system.BaseDexClassLoader", "findClass", String::class.java).invoke(module)
    }

    private fun hookLoader(
        className: String,
        method: String,
        vararg types: Class<*>
    ): (XposedModule) -> Unit = { module ->
        try {
            val c = Class.forName(className, false, null)
            val m = c.getDeclaredMethod(method, *types)
            m.isAccessible = true
            module.hook(m).intercept { chain ->
                val name = try {
                    chain.args[0] as? String
                } catch (t: Throwable) {
                    null
                }
                val r = chain.proceed()
                if (name == TARGET) {
                    try {
                        hookClass(module, r as Class<*>)
                    } catch (t: Throwable) {
                        log("hookClass fail: " + t)
                    }
                }
                r
            }
            log("loader entry hooked: " + className + "." + method)
        } catch (t: Throwable) {
            log("hook " + className + "." + method + " fail: " + t)
        }
    }

    fun attachManual(loader: ClassLoader) {
        val module = moduleRef
        if (module == null) {
            log("manual attach: module not ready")
            return
        }
        try {
            val cls = loader.loadClass(TARGET)
            hookClass(module, cls)
        } catch (t: Throwable) {
            log("manual attach fail: " + t)
        }
    }

    private fun hookClass(module: XposedModule, cls: Class<*>) {
        if (hookedClass || hooking) return
        hooking = true
        try {
            doHookClass(module, cls)
        } finally {
            hooking = false
        }
    }

    @Volatile
    private var hooking = false

    private fun doHookClass(module: XposedModule, cls: Class<*>) {
        if (hookedClass) return
        val methods = try {
            cls.declaredMethods
        } catch (t: Throwable) {
            log("declaredMethods fail: " + t)
            return
        }
        var n = 0
        for (m in methods) {
            if (m.name !in WHITELIST) continue
            if (java.lang.reflect.Modifier.isAbstract(m.modifiers)) continue
            val key = m.toGenericString()
            if (!hooked.add(key)) continue
            try {
                m.isAccessible = true
                module.hook(m).intercept { chain -> trace(chain) }
                n++
            } catch (t: Throwable) {
                log("hook " + m.name + " fail: " + t)
            }
        }
        hookedClass = n > 0
        log("hooked " + n + " in " + cls.name)
    }

    private fun trace(chain: XposedInterface.Chain): Any? {
        val exec = chain.executable
        val method = exec.name
        val args = try {
            chain.args
        } catch (t: Throwable) {
            null
        }
        val argStr = args?.joinToString(", ") { fmt(it) } ?: "?"
        val t0 = System.nanoTime()
        var ret: Any? = null
        var err: Throwable? = null
        try {
            ret = chain.proceed()
        } catch (t: Throwable) {
            err = t
            throw t
        } finally {
            val us = (System.nanoTime() - t0) / 1000
            val sb = StringBuilder()
            sb.append(method).append('(').append(argStr).append(')')
            if (err != null) sb.append("  !! ").append(err.javaClass.simpleName).append(": ").append(err.message)
            else sb.append(" -> ").append(fmt(ret))
            sb.append("  [").append(us).append("us]")
            log(sb.toString(), false)
            if (err == null) {
                val r = ret
                if (r is String) maybeDump(method, r)
            }
        }
        val injected = inject(method)
        return if (injected != null) injected else ret
    }

    private fun inject(method: String): String? {
        if (method != "getJFSauthJson") return null
        val ctx = appCtx ?: return null
        if (!CookiePrefs.enabled(ctx)) return null
        if (!CookiePrefs.autoLogin(ctx)) return null
        val data = cachedCookie(ctx) ?: return null
        log("inject getJFSauthJson <- " + CookiePrefs.selected(ctx) + " len=" + data.length, false)
        return data
    }

    @Volatile
    private var cacheName: String = ""

    @Volatile
    private var cacheData: String? = null

    @Volatile
    private var cacheAt: Long = 0L

    private fun cachedCookie(ctx: Context): String? {
        val name = CookiePrefs.selected(ctx)
        if (name.isEmpty()) return null
        val now = System.currentTimeMillis()
        if (name == cacheName && now - cacheAt < 1000L) return cacheData
        cacheName = name
        cacheAt = now
        cacheData = CookiePrefs.selectedData(ctx)
        return cacheData
    }

    private fun maybeDump(method: String, value: String) {
        if (!armCapture) return
        if (value.length < 20) return
        val ctx = appCtx ?: return
        if (!dumped.add(method + "|" + value.hashCode())) return
        armCapture = false
        val ts = SimpleDateFormat("HHmmss", Locale.US).format(Date())
        val base = accountName(value).ifEmpty { "捕获-" + ts }
        val name = CookieStore.withExt(base)
        val ok = CookieStore.write(ctx, name, value)
        log("capture -> cookies/" + CookieStore.sanitize(name) + " ok=" + ok + " len=" + value.length)
    }

    private fun accountName(sauthJson: String): String {
        return try {
            val o = org.json.JSONObject(sauthJson)
            val u = o.optString("username", "")
            if (u.isNotEmpty()) {
                u.substringBefore('@')
            } else {
                o.optString("sdkuid", "")
            }
        } catch (t: Throwable) {
            ""
        }
    }

    private fun fmt(v: Any?): String {
        if (v == null) return "null"
        val s = when (v) {
            is String -> v
            is ByteArray -> "byte[" + v.size + "]"
            is CharArray -> "char[" + v.size + "]"
            is Array<*> -> "[" + v.size + "]" + v.joinToString(",", "[", "]") { fmt(it) }
            else -> v.toString()
        }
        return if (s.length > MAX_VALUE) s.substring(0, MAX_VALUE) + "…(共" + s.length + ")" else s
    }

    private fun log(line: String) = log(line, true)

    private fun log(line: String, toFile: Boolean) {
        Log.i(TAG, line)
        if (!toFile) return
        val f = logFile ?: return
        val ts = timeFmt.get()?.format(Date()) ?: ""
        val text = ts + " " + line
        try {
            io.execute {
                try {
                    f.appendText(text + "\n")
                } catch (t: Throwable) {
                    Log.e(TAG, "append fail", t)
                }
            }
        } catch (t: Throwable) {
        }
    }
}
