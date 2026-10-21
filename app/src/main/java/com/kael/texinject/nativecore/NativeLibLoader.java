package com.kael.texinject.nativecore;

import android.text.TextUtils;
import android.util.Log;

public final class NativeLibLoader {
    private static final String TAG = "NativeLibLoader";
    public static final String LIB_NAME = "texinject";
    public static final String FIND_LIB_METHOD = "findLibrary";
    private static volatile Throwable lastLoadError;

    private NativeLibLoader() {}

    public static boolean load(Class<?> moduleClass) {
        if (com.kael.texinject.PreviewMode.isActive) return false;
        if (NativeCore.isNativeReady()) return true;
        lastLoadError = null;
        try {
            ClassLoader moduleLoader = moduleClass.getClassLoader();
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
