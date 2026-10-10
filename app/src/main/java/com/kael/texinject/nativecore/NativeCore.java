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

/** Native bridge intentionally limited to the independent building-tools API. */
public final class NativeCore {
    private static final String TAG = "TexInjectNative";
    private static final Object SO_LOAD_LOCK = new Object();
    private static volatile boolean soLoaded;
    private static volatile Throwable soLoadError;

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

    /** 安装共享原生 hook（Actor::normalTick 等，箱子光环等需要）。 */
    public static native void ensureNativeHooks();

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
}
