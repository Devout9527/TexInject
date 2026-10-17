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
// bridge.cpp —— 在 LSPosed 模块内驱动内置原生核心库（core.so）的 ks_entry。
//
// 协议（对齐跑路 JS API 的 kuNative 桥）：
//   dlopen(core.so) -> dlsym("ks_entry") -> 自检(op63/52/61)
//   op40 取 payload 缓冲地址 -> 写入 [4字节小端长度][payload] -> op39 执行注入/还原
//
// ks_entry 调用约定：ks_entry(op<<24 | value)，返回 long。
// payload = resDir + "\n" + chan + "\n" + filesRoot + "\n"
#include <jni.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <android/log.h>

#define LOG_TAG "texinject"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

typedef long (*ks_entry_fn)(long);

// 内置的 core.so 字节（core_blob.S 里 .incbin）
extern "C" const unsigned char texinject_core_so_start[];
extern "C" const unsigned char texinject_core_so_end[];

static void *g_handle = nullptr;
static ks_entry_fn g_entry = nullptr;
static char g_loadedPath[1024] = {0};

static bool ensure_loaded(const char *soPath) {
    if (g_entry && strcmp(g_loadedPath, soPath) == 0) return true;

    void *h = dlopen(soPath, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        LOGE("dlopen fail: %s", dlerror());
        return false;
    }
    ks_entry_fn fn = (ks_entry_fn) dlsym(h, "ks_entry");
    if (!fn) {
        LOGE("dlsym ks_entry fail: %s", dlerror());
        dlclose(h);
        return false;
    }

    // 自检
    fn(63L << 24 | 0xABCDEF);
    long boot = fn(52L << 24);
    long self = fn(61L << 24);
    LOGI("core.so loaded, boot=%ld selfcheck=%ld", boot, self);

    g_handle = h;
    g_entry = fn;
    strncpy(g_loadedPath, soPath, sizeof(g_loadedPath) - 1);
    return true;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_kael_texinject_NativeInjector_nativeInject(
        JNIEnv *env, jobject /*thiz*/,
        jstring jSoPath, jstring jResDir, jstring jChan, jstring jFilesRoot, jint mode) {

    const char *soPath = env->GetStringUTFChars(jSoPath, nullptr);
    const char *resDir = env->GetStringUTFChars(jResDir, nullptr);
    const char *chan = env->GetStringUTFChars(jChan, nullptr);
    const char *filesRoot = env->GetStringUTFChars(jFilesRoot, nullptr);

    char result[256];
    bool ok = false;

    if (!ensure_loaded(soPath)) {
        snprintf(result, sizeof(result), "ERR dlopen: %s", dlerror() ? dlerror() : "unknown");
        goto done;
    }

    {
        long bufAddr = g_entry(40L << 24);
        if (bufAddr == 0) {
            snprintf(result, sizeof(result), "ERR bufAddr=0");
            goto done;
        }
        char payload[4096];
        int n = snprintf(payload, sizeof(payload), "%s\n%s\n%s\n", resDir, chan, filesRoot);
        if (n <= 0 || n >= (int) sizeof(payload)) {
            snprintf(result, sizeof(result), "ERR payload too long");
            goto done;
        }
        unsigned char *p = (unsigned char *) (intptr_t) bufAddr;
        p[0] = n & 0xFF;
        p[1] = (n >> 8) & 0xFF;
        p[2] = (n >> 16) & 0xFF;
        p[3] = (n >> 24) & 0xFF;
        memcpy(p + 4, payload, n);

        g_entry((39L << 24) | (mode & 0xFFFFFF));
        snprintf(result, sizeof(result), "OK");
        ok = true;
    }

done:
    env->ReleaseStringUTFChars(jSoPath, soPath);
    env->ReleaseStringUTFChars(jResDir, resDir);
    env->ReleaseStringUTFChars(jChan, chan);
    env->ReleaseStringUTFChars(jFilesRoot, filesRoot);
    return env->NewStringUTF(result);
}

// 把内置的 core.so 字节写到 soPath（若文件缺失或大小不符）。
extern "C" JNIEXPORT jboolean JNICALL
Java_com_kael_texinject_NativeInjector_nativeEnsureCore(
        JNIEnv *env, jobject /*thiz*/, jstring jSoPath) {
    const char *soPath = env->GetStringUTFChars(jSoPath, nullptr);
    if (!soPath) return JNI_FALSE;
    const unsigned char *begin = texinject_core_so_start;
    const unsigned char *end = texinject_core_so_end;
    const size_t want = (size_t) (end - begin);
    bool ok = false;
    // 大小一致就认为已是同一份
    FILE *chk = fopen(soPath, "rb");
    if (chk) {
        fseek(chk, 0, SEEK_END);
        long sz = ftell(chk);
        fclose(chk);
        if (sz == (long) want) {
            env->ReleaseStringUTFChars(jSoPath, soPath);
            return JNI_TRUE;
        }
    }
    FILE *out = fopen(soPath, "wb");
    if (out) {
        size_t written = fwrite(begin, 1, want, out);
        fclose(out);
        ok = (written == want);
        if (ok) chmod(soPath, 0755);
    }
    if (!ok) LOGE("nativeEnsureCore write fail: %s", soPath);
    env->ReleaseStringUTFChars(jSoPath, soPath);
    return ok ? JNI_TRUE : JNI_FALSE;
}
