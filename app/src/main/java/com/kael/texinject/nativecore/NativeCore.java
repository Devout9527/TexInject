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
package com.kael.texinject.nativecore;

import android.util.Log;

/** Native bridge：Python 执行桥 + 基址解析。 */
public final class NativeCore {
    private static final String TAG = "TexInjectNative";
    private static final Object SO_LOAD_LOCK = new Object();
    private static volatile boolean soLoaded;
    private static volatile Throwable soLoadError;

    // 类一被触碰就加载 native 库，否则某些路径直接调 runPythonSource 会报
    // "No implementation found for ... runPythonSource"。
    static {
        loadNative();
    }

    private NativeCore() {}

    public static boolean loadNative() {
        if (soLoaded) return true;
        synchronized (SO_LOAD_LOCK) {
            if (soLoaded) return true;
            try {
                final String libraryName = DevConfig.SO_NAM;
                if (libraryName == null || libraryName.trim().isEmpty()) {
                    throw new IllegalStateException("Native library name is empty");
                }
                System.loadLibrary(libraryName);
                soLoaded = true;
                soLoadError = null;
            } catch (Throwable throwable) {
                soLoaded = false;
                soLoadError = throwable;
                Log.e(TAG, "Unable to load TexInject native runtime", throwable);
            }
            return soLoaded;
        }
    }

    /** Used by the optional injected host, where the module APK owns the .so. */
    static boolean loadNativePath(String absolutePath) {
        if (soLoaded) return true;
        synchronized (SO_LOAD_LOCK) {
            if (soLoaded) return true;
            try {
                System.load(absolutePath);
                soLoaded = true;
                soLoadError = null;
            } catch (Throwable throwable) {
                soLoaded = false;
                soLoadError = throwable;
                Log.e(TAG, "Unable to load TexInject native runtime", throwable);
            }
            return soLoaded;
        }
    }

    public static boolean isNativeReady() {
        return soLoaded;
    }

    /** Returns the most recent native-loader error for injected-host diagnostics. */
    public static Throwable getNativeLoadError() {
        return soLoadError;
    }

    /** 安装共享原生 hook。 */
    public static native void ensureNativeHooks();

    /** 传入游戏 versionName（3.9.15.297907 / 3.10.0.300878），用于选 Python 地址表。 */
    public static native void setGameVersion(String version);

    /**
     * 在游戏 Python 解释器(__main__)里执行指定 .py 文件的内容。
     * 脚本可 import mod.client.extraClientApi 等网易 Mod API。
     * @param absolutePath .py 文件绝对路径
     * @return true=执行成功
     */
    public static native boolean runPythonFile(String absolutePath);

    /**
     * 在游戏 Python 解释器(__main__)里执行任意源码字符串。
     * 需要基址就绪（同 runPythonFile）。
     */
    public static native boolean runPythonSource(String source);

    /** 自动不死图腾：把背包里 minecraft:totem_of_undying 搬到副手。
     *  返回 "OK,<requestId>" 或 "ERR,<原因>"。 */

    // ==================== PLT / GOT hook ====================

    /**
     * 解析 libminecraftpe.so 的重定位表，缓存所有导入符号的 GOT 槽。
     *
     * @return 解析到的导入符号数量；0 表示失败（模块还没被加载，或解析出错）。
     *         可以反复调用：失败时会在下次重新尝试。
     */
    public static native int pltInit();

    /** Plt.init 失败时的原因。 */
    public static native String pltLastError();

    // ==================== 文件 I/O 追踪（开发者工具） ====================

    /**
     * 开始记录游戏的文件访问（openat / open / fopen / stat / lstat / access / faccessat）。
     * 记录只进内存环形缓冲，不会自己写盘。
     */
    public static native boolean ioTraceStart();

    /** 摘掉 hook。缓冲里的记录保留，仍然可以 dump。 */
    public static native void ioTraceStop();

    public static native boolean ioTraceRunning();

    /** 缓冲里已有的记录条数（上限 4096，超过后饱和）。 */
    public static native int ioTraceCount();

    /** 把缓冲写成文本（rva,kind,ret,path 每行一条）。 */
    public static native boolean ioTraceDump(String absolutePath);

    public static native String ioTraceLastError();

    // ==================== 伪自定义目录映射 ====================

    /**
     * 配置「把某个游戏本来就会加载的资源包目录，映射到我们自己的物理目录」。
     *
     * @param suppliedDataDir  游戏 dataDir 的原始写法（context.applicationInfo.dataDir）
     * @param canonicalDataDir 同一目录的规范路径（getCanonicalPath，去掉符号链接差异）
     * @param aliasSuffix      相对 dataDir 的发现别名，如
     *                         "/files/games/com.netease/resource_packs/3.9_FirstPatch_..."
     * @param storagePath      物理目录绝对路径（可以是外部存储）
     * @return 配置是否被接受（进程内只接受一次，配置不同则拒绝）
     */
    public static native boolean mountConfigure(String suppliedDataDir, String canonicalDataDir,
                                                String aliasSuffix, String storagePath);

    /** 把 GOT 里 11 个 libc 导入换掉。SO 还没装载、或导入不全时返回 false。 */
    public static native boolean mountInstall();

    /** not_configured / waiting_for_file_imports / mounted。 */
    public static native String mountStatus();

    // ---- 内存读写（mem/MemRw.cpp） ----
    /** 把 storge/stream 的读写整体镜像到 root（下载捕获）。 */
    public static native boolean ioTraceMirror(String root);

    /** 取已加载 so 的基址；name 只给文件名，如 libminecraftpe.so。 */
    public static native long memBase(String name);

    public static native int memReadI32(long addr);
    public static native long memReadI64(long addr);
    public static native float memReadF32(long addr);
    public static native double memReadF64(long addr);
    public static native byte[] memReadBytes(long addr, int length);
    public static native String memReadCString(long addr, int max);

    public static native boolean memWriteI32(long addr, int value);
    public static native boolean memWriteI64(long addr, long value);
    public static native boolean memWriteF32(long addr, float value);
    public static native boolean memWriteF64(long addr, double value);
    public static native boolean memWriteBytes(long addr, byte[] data);

    // ---- 游戏侧开关（game/GameApi.cpp）：现在只剩相机穿墙 ----
    /** 开/关：cameraClip。返回 false = 版本不支持或 hook 装不上。 */
    public static native boolean gameFeature(String id, boolean on);

    public static native boolean gameFeatureOn(String id);

    /** 手动装 LocalPlayer hook（幂等）。返回是否就绪。 */
    public static native boolean gameAttach();

    /** 手上物品的 damage；读不到返回 -2147483648。 */
    public static native int gameHandDamage();

    /** 改手上物品 damage。负数合法（没损耗默认是 0）。 */
    public static native boolean gameSetDamage(int value);

    /** 诊断：手上 ItemStack 的地址和头 64 字节。 */
    public static native String gameHandInfo();

    /**
     * 把展平好的 SNBT 指令挂到手上物品的 user data。
     * 只写客户端内存，服务器不认；物品已有 tag 时不覆盖。
     */
    public static native boolean gameApplyNbt(String ops);

    /**
     * Python 加载器（3.10.5）：私有 CPython 编译 → 游戏 opcode 表 → 游戏 marshal
     * 读入 → 在 Player tick 上 PyEval_EvalCode。
     *
     * 这四个必须和 native-lib.cpp 的 native_methods 一一对上：
     * RegisterNatives 找不到方法就整体失败，JNI_OnLoad 返回 JNI_ERR，
     * 整个 so 都加载不了（表现是「原生库加载失败」，不止 py 不能用）。
     */
    public static native boolean pyInit();

    public static native boolean pyReady();

    public static native boolean pyStart(byte[] utf8Path);

    public static native byte[] pyStatus();
}
