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

/**
 * LSPosed 模块入口。
 *
 * 作用域：网易版 Minecraft（com.netease.x19）。
 * 行为：在游戏 Activity 创建后，向其 decorView 注入可拖动的悬浮按钮，
 *      点击打开材质包注入面板（注入/还原/重启游戏）。
 */
class TexInjectHookInit : XposedModule() {

    init {
        instance = this
    }

    /**
     * 现代 LibXposed API（api-102）入口，等价于旧版 handleLoadPackage。
     * 部分 MIUI 设备会把 contentcatcher 等辅助包回调进来，仅按包名放行游戏主包；
     * 真实 Activity 在 onCreate 时再校验。
     */
    override fun onPackageLoaded(param: PackageLoadedParam) {
        if (param.packageName != GamePaths.MINECRAFT_PACKAGE) return
        bridgeLog("LoadPackage: ${param.packageName} (api=${apiVersion})")
        installActivityHook()
    }

    private fun installActivityHook() {
        if (!hooksInstalled.compareAndSet(false, true)) return
        try {
            // Activity 属于 boot classloader，直接传其 Class，避免
            // LoadPackage 的 classLoader 指向辅助包导致找不到方法。
            val onCreate = Activity::class.java.getDeclaredMethod("onCreate", Bundle::class.java)
            hook(onCreate).intercept { chain ->
                // 先执行原 onCreate，再在原方法返回后做注入（等价旧版 afterHookedMethod）
                val result = chain.proceed()
                val activity = chain.thisObject as? Activity
                if (activity != null && activity.javaClass.name == GamePaths.MINECRAFT_ACTIVITY) {
                    bridgeLog("Minecraft MainActivity created; 10 秒后注入悬浮按钮")
                    activityRef = WeakReference(activity)
                    ensureSourceDir(activity)
                    // 延迟 10 秒再注入悬浮窗，避开游戏启动瞬间（启动阶段容易被反作弊/引擎初始化干扰）
                    android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                        try {
                            if (!activity.isFinishing && !activity.isDestroyed) {
                                OverlayManager.attach(activity)
                                // 启动计数 + 更新检查 / 公告 / 求 star
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
                    }, 10_000L)
                }
                result
            }
            // 放行明文 HTTP：音乐 API 只有 http://（Android 9+ 默认禁止），
            // 而网络策略是【宿主进程】的，模块自己的清单改不了，只能 hook 掉判断。
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

    /** 初始化材质包源目录并写入使用说明（首次）。 */
    private fun ensureSourceDir(activity: Activity) {
        try {
            GamePaths.init(activity)
            val source = GamePaths.sourceDir // /storage/emulated/0/Android/data/com.netease.x19/files/resource_packs
            val readme = com.kael.texinject.paths.BuildPaths.readmeFile(activity)
            if (true) { // 内容随版本更新，总是重写
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

                    ── 建筑工具目录（同级的 pack_netease/build）──
                      • 导入 / 导出 / 投影 全部共用这一个目录（不再分子目录）
                        .litematic / .schematic / .bdx / 图片 / MIDI 等都直接放这里

                    ── Python 脚本（同级的 pack_netease/scripts）──
                      • 放 .py 文件 → 面板「脚本」里点击执行
                      • 在游戏 Python 解释器里跑，可 import mod.client.extraClientApi 等网易 Mod API

                    ── 面板分类 ──
                      材质替换：注入资源 / 恢复官方 / 替换视频 / 恢复视频 / 重载外观 / 重启游戏
                      音乐：在线播放 / 扫码登录 / 歌词显示（播放条图标、背景可自定义）
                      脚本：py / mcp 两类切换（py 直接执行，mcp 用内置加载器注册加载）
                      快捷键：列出 MCP 模块并直接开关，可给模块挂悬浮窗（点击开关）
                      设置：音乐播放模式 / 播放条尺寸 / 面板布局 / 分类图标大小

                    ── 声明 ──
                      • 材质包注入、视频替换功能来自 Kusug UI（作者：bi匕匕bi）。

                    放入或改名后，在面板点「重载外观」即时刷新（视频/材质需重启游戏生效）。

                    本目录路径：
                    /storage/emulated/0/Android/data/com.netease.x19/files/pack_netease/resource_packs/
                    """.trimIndent()
                )
            }
            // 合并说明：整份生成到 pack_netease/ 下
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
            // 框架未 attach 时忽略
        }
    }

    companion object {
        const val TAG = "TexInject"
        val hooksInstalled = AtomicBoolean(false)

        @Volatile
        var activityRef: WeakReference<Activity>? = null

        @Volatile
        private var instance: TexInjectHookInit? = null

        /** 给非模块类（如 OverlayManager）用的日志入口；未就绪时退化为 logcat。 */
        fun logExternal(msg: String) {
            try {
                instance?.log(Log.INFO, TAG, msg)
            } catch (_: Throwable) {
            }
            Log.i(TAG, msg)
        }
    }
}
