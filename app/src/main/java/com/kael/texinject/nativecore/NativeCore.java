package com.kael.texinject.nativecore;

import android.util.Log;

public final class NativeCore {
    private static final String TAG = "TexInjectNative";
    private static final Object SO_LOAD_LOCK = new Object();
    private static volatile boolean soLoaded;
    private static volatile Throwable soLoadError;

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

    public static Throwable getNativeLoadError() {
        return soLoadError;
    }

    public static native void ensureNativeHooks();

    public static native void setGameVersion(String version);

    public static native boolean runPythonFile(String absolutePath);

    public static native boolean runPythonSource(String source);

}
