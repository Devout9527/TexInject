/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 临时探针：只观测、不改写。用来定位网易登录 sauth_json 在 Java 层的真实入口。
 *
 * 重要：绝不主动 Class.forName 目标类。
 *   ART 会缓存「类加载失败」的结果，一旦在游戏自己加载 UniSDK 之前抢先加载并失败，
 *   之后游戏再加载同一个类会直接拿到缓存的 NoClassDefFoundError → 卡死登录流程。
 *   所以这里只 hook ClassLoader.loadClass，等游戏自己加载成功后顺手挂方法。
 */
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

    /** 网易 UniSDK 的基类，sauth 相关入口几乎都在这里。 */
    private const val TARGET = "com.netease.ntunisdk.base.SdkBase"

    /** 逃生开关：存在这个文件就完全不挂探针。 */
    private const val ESCAPE = "/data/local/tmp/texinject_no_probe"

    /**
     * 只挂这些方法。名字必须精确匹配 —— 之前按 "auth"/"json" 模糊匹配，
     * 会把 isAuthValid 之类的高频方法也挂上，日志写盘直接把主线程拖死。
     */
    private val WHITELIST = setOf(
        "getJFSauthJson",
        "setBackSauthLoginJson",
        "setLoginSauthInfo",
        "setJFSauth",
        "setJFSauthWithKey",
        "getJfSauthChannelMap",
        // 公告：SDK 拿到公告后回调 getNoticeMsgDone(String json)，是替换公告的入口
        "getNoticeMsgDone",
        "getAnnouncementInfo",
        "getNotice"
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

    /** 目标类方法是否已经挂上。 */
    @Volatile
    var hookedClass: Boolean = false
        private set

    private val dumped = Collections.synchronizedSet(HashSet<String>())

    /** UI 点「捕获下一次登录」后置 true。 */
    @Volatile
    var armCapture: Boolean = false

    /** 写日志用后台线程，避免在主线程做文件 IO。 */
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

    /**
     * 只在包加载后调用一次：挂类加载入口，等游戏自己加载 SdkBase。
     * 这里不碰目标类，所以不会污染 ART 的类加载缓存。
     *
     * 注意：3.10 的 UniSDK 在动态解压的 dex 里（app_ntp0/.../.unzip/classes*.dex），
     * 走的是 BaseDexClassLoader 覆写过的 loadClass，光挂 ClassLoader 截不到，两个都要挂。
     */
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

    /** 返回一个「装完就生效」的闭包，统一处理异常。 */
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

    /**
     * 手动挂载：用户已经进游戏（UniSDK 必定加载完）之后调用，此时 loadClass 走缓存，安全。
     * 用 UI 上的「挂载探针」按钮触发。
     */
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
            log(sb.toString())
            if (err == null) {
                val r = ret
                if (r is String) maybeDump(method, r)
            }
        }
        val injected = inject(method)
        return if (injected != null) injected else ret
    }

    /**
     * 回填：把选中的 cookie 当成 getJFSauthJson() 的返回值交出去。
     * 先让原逻辑跑完（SDK 内部状态该同步的同步），只替换返回值。
     */
    private fun inject(method: String): String? {
        if (method != "getJFSauthJson") return null
        val ctx = appCtx ?: return null
        if (!CookiePrefs.enabled(ctx)) return null
        if (!CookiePrefs.autoLogin(ctx)) return null
        val data = cachedCookie(ctx) ?: return null
        log("inject getJFSauthJson <- " + CookiePrefs.selected(ctx) + " len=" + data.length)
        return data
    }

    @Volatile
    private var cacheName: String = ""

    @Volatile
    private var cacheData: String? = null

    @Volatile
    private var cacheAt: Long = 0L

    /** 选中的 cookie 正文，1 秒内复用，避免每次调用都读文件。 */
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

    /**
     * 只有点了「捕获下一次登录」才落盘，捕一次就停。
     * 之前是「看到像 sauth 就存」，导致每登录一次就多一个文件，看起来永远停不下来。
     */
    private fun maybeDump(method: String, value: String) {
        if (!armCapture) return
        if (value.length < 20) return
        val ctx = appCtx ?: return
        if (!dumped.add(method + "|" + value.hashCode())) return
        armCapture = false
        val ts = SimpleDateFormat("HHmmss", Locale.US).format(Date())
        // 文件名 = 账号 ID（sauth_json 里的 sdkuid / username），拿不到才退回时间
        val base = accountName(value).ifEmpty { "捕获-" + ts }
        val name = CookieStore.withExt(base)
        val ok = CookieStore.write(ctx, name, value)
        log("capture -> cookies/" + CookieStore.sanitize(name) + " ok=" + ok + " len=" + value.length)
    }

    /** 从 sauth_json 里取账号名：优先 username（去掉邮箱后缀），没有就用 sdkuid。 */
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

    private fun log(line: String) {
        Log.i(TAG, line)
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
            // 线程池已关（进程退出中），忽略
        }
    }
}
