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

import android.text.TextUtils;
import android.util.Log;

/**
 * Loads the module-owned native library in an Xposed/LSPosed host process.
 *
 * The module ClassLoader, not the game's ClassLoader, owns the native library
 * path. Keep this deliberately small: it is the same resolution route used by
 * the known-good building-tools release.
 */
public final class NativeLibLoader {
    private static final String TAG = "NativeLibLoader";
    public static final String LIB_NAME = "texinject";
    public static final String FIND_LIB_METHOD = "findLibrary";
    private static volatile Throwable lastLoadError;

    private NativeLibLoader() {}

    public static boolean load(Class<?> moduleClass) {
        // 预览模式（桌面入口）：只渲染 UI，绝不加载 native 注入库
        if (com.kael.texinject.PreviewMode.isActive) return false;
        if (NativeCore.isNativeReady()) return true;
        lastLoadError = null;
        try {
            // Ask LSPosed's module ClassLoader for the installed module's
            // actual native-library path, then load that exact path.
            ClassLoader moduleLoader = moduleClass.getClassLoader();
            // findLibrary 是 protected：走反射并 setAccessible（原为 XposedHelpers.callMethod）
            java.lang.reflect.Method findLib =
                    moduleLoader.getClass().getDeclaredMethod(FIND_LIB_METHOD, String.class);
            findLib.setAccessible(true);
            String libPath = (String) findLib.invoke(moduleLoader, LIB_NAME);
            if (TextUtils.isEmpty(libPath)) {
                lastLoadError = new UnsatisfiedLinkError(
                        "Module ClassLoader did not find " + System.mapLibraryName(LIB_NAME));
                Log.e(TAG, "Native library not found: " + LIB_NAME);
                return false;
            }
            if (NativeCore.loadNativePath(libPath)) {
                Log.d(TAG, "Native library loaded: " + libPath);
                try {
                    NativeCore.ensureNativeHooks();
                } catch (Throwable hookError) {
                    Log.e(TAG, "ensureNativeHooks failed", hookError);
                }
                return true;
            }
            lastLoadError = NativeCore.getNativeLoadError();
            Log.e(TAG, "Native library could not be loaded: " + libPath, lastLoadError);
        } catch (Throwable error) {
            lastLoadError = error;
            Log.e(TAG, "Native library lookup/load failed", error);
        }
        return false;
    }

    public static Throwable getLastLoadError() {
        return lastLoadError;
    }
}
