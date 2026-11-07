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

    public static native int pltInit();

    public static native String pltLastError();

    public static native boolean ioTraceStart();

    public static native void ioTraceStop();

    public static native boolean ioTraceRunning();

    public static native int ioTraceCount();

    public static native boolean ioTraceDump(String absolutePath);

    public static native String ioTraceLastError();

    public static native boolean mountConfigure(String suppliedDataDir, String canonicalDataDir,
                                                String aliasSuffix, String storagePath);

    public static native boolean mountInstall();

    public static native String mountStatus();

    public static native boolean ioTraceMirror(String root);

    public static native boolean ioTraceMirrorOn();

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

    public static native boolean gameFeature(String id, boolean on);

    public static native boolean gameFeatureOn(String id);

    public static native boolean gameAttach();

    public static native boolean gameInWorld();

    public static native int gameHandDamage();

    public static native boolean gameSetDamage(int value);

    public static native String gameHandInfo();

    public static native boolean gameApplyNbt(String ops);

    public static native boolean pyInit();

    public static native boolean pyReady();

    public static native boolean pyStart(byte[] utf8Path);

    public static native byte[] pyStatus();
}
