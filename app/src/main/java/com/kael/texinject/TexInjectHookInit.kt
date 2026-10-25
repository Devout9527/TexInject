package com.kael.texinject

import android.app.Activity
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import com.kael.texinject.ui.OverlayManager
import io.github.libxposed.api.XposedModule
import io.github.libxposed.api.XposedModuleInterface.PackageLoadedParam
import java.io.File
import java.lang.ref.WeakReference
import java.util.concurrent.atomic.AtomicBoolean

class TexInjectHookInit : XposedModule() {

    init {
        instance = this
    }

    override fun onPackageLoaded(param: PackageLoadedParam) {
        if (param.packageName !in GamePaths.SUPPORTED_PACKAGES) return
        bridgeLog("LoadPackage: ${param.packageName} (api=${apiVersion})")
        installActivityHook()
        try {
            SauthProbe.install(this)
        } catch (e: Throwable) {
            bridgeLog("sauth probe install failed", e)
        }
    }

    private fun installActivityHook() {
        if (!hooksInstalled.compareAndSet(false, true)) return
        try {
            val onCreate = Activity::class.java.getDeclaredMethod("onCreate", Bundle::class.java)
            hook(onCreate).intercept { chain ->
                val result = chain.proceed()
                val activity = chain.thisObject as? Activity
                if (activity != null && activity.javaClass.name == GamePaths.MINECRAFT_ACTIVITY) {
                    bridgeLog("Minecraft MainActivity created; 7.5 秒后注入悬浮按钮")
                    activityRef = WeakReference(activity)
                    ensureSourceDir(activity)
                    try {
                        SauthProbe.init(activity)
                    } catch (e0: Throwable) {
                        bridgeLog("sauth probe init failed", e0)
                    }
                    android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                        try {
                            if (!activity.isFinishing && !activity.isDestroyed) {
                                try {
                                    SauthProbe.attachManual(activity.classLoader)
                                } catch (e5: Throwable) {
                                    bridgeLog("sauth probe re-attach failed", e5)
                                }
                                OverlayManager.attach(activity)
                                try {
                                    com.kael.texinject.LaunchCounter.bump(activity)
                                    com.kael.texinject.UpdateUi.checkAndShow(activity, true)
                                    com.kael.texinject.UpdateUi.maybeAskStar(activity)
                                } catch (e3: Throwable) {
                                    bridgeLog("update check failed", e3)
                                }
                            }
                        } catch (e: Throwable) {
                            bridgeLog("delayed attach failed", e)
                        }
                    }, OVERLAY_DELAY_MS)
                }
                result
            }
            try {
                val np = Class.forName("android.security.NetworkSecurityPolicy")
                try {
                    val m0 = np.getDeclaredMethod("isCleartextTrafficPermitted")
                    hook(m0).intercept { java.lang.Boolean.TRUE }
                } catch (e1: Throwable) {
                }
                try {
                    val m1 = np.getDeclaredMethod("isCleartextTrafficPermitted", String::class.java)
                    hook(m1).intercept { java.lang.Boolean.TRUE }
                } catch (e2: Throwable) {
                }
                bridgeLog("cleartext HTTP allowed (hook NetworkSecurityPolicy)")
            } catch (e: Throwable) {
                bridgeLog("cleartext hook failed", e)
            }
            bridgeLog("Activity onCreate hook installed")
        } catch (e: Throwable) {
            hooksInstalled.set(false)
            bridgeLog("install activity hook failed", e)
        }
    }

    private fun ensureSourceDir(activity: Activity) {
        try {
            GamePaths.init(activity)
            try {
                com.kael.texinject.nativecore.NativeCore.setGameVersion(GamePaths.version)
            } catch (e0: Throwable) {
                bridgeLog("setGameVersion failed", e0)
            }
            val source = GamePaths.sourceDir
            val readme = com.kael.texinject.paths.BuildPaths.readmeFile(activity)
            if (true) {
                readme.writeText(
                    """
                    本目录（pack_netease/resource_packs）用于放置材质包，可用文件管理器直接放入。
                    自定义图标/背景/悬浮球视频只认 pack_netease/icon/ 目录（不再扫 resource_packs），详见 icon/说明.txt）。

                    ── 材质包 ──
                      • .mcpack / .zip  → 面板「材质 → 注入资源」

                    ── 可自定义的外观（放到 pack_netease/icon/，名字要对）──
                      • 悬浮球图标：Icon.png / Logo.png / Cover.png
                      • 面板背景图：background.png / bg.png / panel.png / menu.png / cover.png / 背景.png / 面板.png
                      • 面板背景视频：background.mp4 / bg.mp4 / 面板.mp4 / 背景.mp4（优先于背景图，循环静音）
                      • 分类图标  ：<分类名>.png（如 材质.png / 导入.png）或 icon1.png…icon7.png
                      • 主页视频  ：loginVideoNew.mp4 / 登录视频.mp4 / 主界面视频.mp4 / home.mp4
                      • 播放条图标：music_icon.png / 音乐图标.png（右上角"正在播放"条）
                      • 播放条背景：music_bar.png（图片）/ music_bar.mp4（视频，循环静音）
                      • 音乐地址  ：music_api.txt（首行写 API 地址，默认 http://musicapi.infinitex.icu）
                      • 音乐登录  ：music_cookie.txt（面板"音乐"里扫码登录后自动生成，换号删掉即可）

                    ── Python 脚本（同级的 pack_netease/scripts）──
                      • 放 .py 文件 → 面板「脚本」里点击执行
                      • 在游戏 Python 解释器里跑，可 import mod.client.extraClientApi 等网易 Mod API

                    ── 面板分类 ──
                      材质替换：注入资源 / 恢复官方 / 替换视频 / 恢复视频 / 重载外观 / 重启游戏
                      音乐：在线播放 / 扫码登录 / 歌词显示（播放条图标、背景可自定义）
                      脚本：py / mcp 两类切换（py 直接执行，mcp 用内置加载器注册加载）
                      快捷键：列出 MCP 模块并直接开关，可给模块挂悬浮窗（点击开关）
                      设置：音乐播放模式 / 播放条尺寸 / 面板布局 / 分类图标大小

                    放入或改名后，在面板点「重载外观」即时刷新（视频/材质需重启游戏生效）。

                    本目录路径：
                    /storage/emulated/0/Android/data/当前游戏包名/files/pack_netease/resource_packs/
                    """.trimIndent()
                )
            }
            try {
                val merged = com.kael.texinject.paths.BuildPaths.readmeFile(activity)
                if (!merged.exists()) {
                    merged.parentFile?.mkdirs()
                    merged.writeText(
                        "TexInject —— Minecraft 中国版（网易版）增强面板\n" +
                        "=================================================\n\n" +
                        "适配游戏版本：3.9.15.297907\n" +
                        "框架：LSPosed（现代 LibXposed API 102） · arm64-v8a\n\n" +
                        "本目录（pack_netease/）结构：\n" +
                        "  icon/            自定义图标 / 背景 / 悬浮球视频 / 音乐条\n" +
                        "  mcp/             MCP 模组（每个可配配置文件，与该 mcp 同级）\n" +
                        "  scripts/         .py 脚本（面板「脚本 → py」里点击执行）\n" +
                        "  resource_packs/  材质包（面板「材质替换」注入）\n\n" +
                        "面板分类：材质替换 / 音乐 / 脚本 / 快捷键 / 设置\n\n" +
                        "许可：GNU AGPL-3.0-or-later\n" +
                        "源码：https://github.com/Devout9527/TexInject\n"
                    )
                }
            } catch (e4: Throwable) {
            }
        } catch (e: Throwable) {
            bridgeLog("ensure source dir failed", e)
        }
    }

    private fun bridgeLog(msg: String, err: Throwable? = null) {
        Log.i(TAG, msg)
        try {
            log(Log.INFO, TAG, msg)
            if (err != null) log(Log.ERROR, TAG, Log.getStackTraceString(err))
        } catch (_: Throwable) {
        }
    }

    companion object {
        const val TAG = "TexInject"

        const val OVERLAY_DELAY_MS = 6_000L

        val hooksInstalled = AtomicBoolean(false)

        @Volatile
        var activityRef: WeakReference<Activity>? = null

        @Volatile
        private var instance: TexInjectHookInit? = null

        fun logExternal(msg: String) {
            try {
                instance?.log(Log.INFO, TAG, msg)
            } catch (_: Throwable) {
            }
            Log.i(TAG, msg)
        }
    }
}
