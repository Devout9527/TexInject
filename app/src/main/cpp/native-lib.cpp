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
// TexInject 原生库（最小化）
// 只保留：Python 执行桥（runPythonFile / runPythonSource）+ 基址解析。
// 建筑导入/导出、地图(anvil/chest)、投影、箱子光环、OpenSSL、Dobby 已全部移除。
#include <jni.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <unistd.h>
#include <link.h>
#include <android/log.h>

#include "main.h"
#include "tp/PythonUtils.h"

#define LOG_TAG "TexInjectNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

std::atomic<uintptr_t> Main::baseAddress{0};

namespace {

bool looksLikeElf(uintptr_t p) {
    if (p == 0) return false;
    return *reinterpret_cast<const uint32_t*>(p) == 0x464c457f;  // \x7fELF
}

struct FindCtx { const char* name; uintptr_t base; };

int findModuleCb(dl_phdr_info* info, size_t, void* data) {
    auto* c = static_cast<FindCtx*>(data);
    if (!info->dlpi_name || !info->dlpi_name[0]) return 0;
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* leaf = slash ? slash + 1 : info->dlpi_name;
    if (std::strcmp(leaf, c->name) != 0) return 0;
    c->base = static_cast<uintptr_t>(info->dlpi_addr);
    return 1;  // 找到就停
}

// load bias 一定等于 ELF 头地址（offset 0 的那一段），所以用 dl_iterate_phdr
// 拿是最可靠的；旧实现直接取 /proc/self/maps 里第一条含名字的行，
// 可能命中 linker 留下的非 offset-0 映射（实测 3.10 上偏了 0x11A5A000），
// 导致 base+offset 全部错位。
uintptr_t getModuleBase(const char* name) {
    if (std::strcmp(name, "libminecraftpe.so") == 0) {
        const uintptr_t b = FunctionsAddress::moduleBase();
        if (b != 0) return b;
    }
    FindCtx ctx{name, 0};
    dl_iterate_phdr(findModuleCb, &ctx);
    if (looksLikeElf(ctx.base)) return ctx.base;

    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    uintptr_t base = 0;
    while (std::fgets(line, sizeof(line), maps)) {
        unsigned long long lo = 0, off = 0;
        char perms[8] = {0};
        if (std::strstr(line, name) != nullptr &&
            std::sscanf(line, "%llx-%*x %7s %llx", &lo, perms, &off) == 3 &&
            off == 0 && looksLikeElf(static_cast<uintptr_t>(lo))) {
            base = static_cast<uintptr_t>(lo);
            break;
        }
    }
    std::fclose(maps);
    return base;
}

void ensureBase() {
    if (Main::getBaseAddress() == 0) {
        Main::setBaseAddress(getModuleBase("libminecraftpe.so"));
    }
}

void waitForMinecraft() {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const uintptr_t base = getModuleBase("libminecraftpe.so");
        if (base != 0) {
            Main::setBaseAddress(base);
            return;
        }
        usleep(500000);
    }
    LOGE("libminecraftpe.so not found");
}

// Kotlin 侧 NativeCore.ensureNativeHooks 调用；最小版只需要基址就绪。
void ensureNativeHooks(JNIEnv*, jclass) { ensureBase(); }

// 执行一个 .py 文件的内容（在游戏 Python 解释器的 __main__ 里）。
jboolean runPythonFile(JNIEnv* env, jclass, jstring jpath) {
    if (!jpath) return JNI_FALSE;
    const char* pathChars = env->GetStringUTFChars(jpath, nullptr);
    if (!pathChars) return JNI_FALSE;
    std::string path(pathChars);
    env->ReleaseStringUTFChars(jpath, pathChars);
    if (path.empty()) return JNI_FALSE;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) { LOGE("runPythonFile: cannot open %s", path.c_str()); return JNI_FALSE; }
    std::string code((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    file.close();
    if (code.empty()) { LOGE("runPythonFile: empty %s", path.c_str()); return JNI_FALSE; }

    ensureBase();
    if (Main::getBaseAddress() == 0) { LOGE("runPythonFile: base not found"); return JNI_FALSE; }
    const bool ok = PythonUtils::PyExecChecked(code, true);
    LOGI("runPythonFile: %s -> %s", path.c_str(), ok ? "ok" : "fail");
    return ok ? JNI_TRUE : JNI_FALSE;
}

// 执行任意 Python 源码字符串。
jboolean runPythonSource(JNIEnv* env, jclass, jstring jsource) {
    if (!jsource) return JNI_FALSE;
    const char* chars = env->GetStringUTFChars(jsource, nullptr);
    if (!chars) return JNI_FALSE;
    std::string source(chars);
    env->ReleaseStringUTFChars(jsource, chars);
    if (source.empty()) return JNI_FALSE;

    ensureBase();
    if (Main::getBaseAddress() == 0) { LOGE("runPythonSource: base not found"); return JNI_FALSE; }
    const bool ok = PythonUtils::PyExecChecked(source, true);
    LOGI("runPythonSource: -> %s", ok ? "ok" : "fail");
    return ok ? JNI_TRUE : JNI_FALSE;
}

const JNINativeMethod native_methods[] = {
    {"ensureNativeHooks", "()V",     reinterpret_cast<void*>(ensureNativeHooks)},
    {"runPythonFile",     "(Ljava/lang/String;)Z", reinterpret_cast<void*>(runPythonFile)},
    {"runPythonSource",   "(Ljava/lang/String;)Z", reinterpret_cast<void*>(runPythonSource)},
};

}  // namespace

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass clazz = env->FindClass("com/kael/texinject/nativecore/NativeCore");
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    const jint registered = env->RegisterNatives(
        clazz, native_methods, sizeof(native_methods) / sizeof(native_methods[0]));
    env->DeleteLocalRef(clazz);
    if (registered != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return JNI_ERR;
    }
    // 3.10（com.netease.mctest）的函数地址尚未补全（setExecLocked 仍为 0），
    // 按 3.9 的偏移去 hook 会跳到野地址 → SIGSEGV 直接闪退。
    // 地址补全前，测试版一律不安装原生 hook：脚本/材质功能不可用，但不会崩。
    {
        FILE* f = fopen("/proc/self/cmdline", "r");
        char cmd[256] = {0};
        bool isTest = false;
        if (f) {
            size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
            fclose(f);
            if (n > 0 && strstr(cmd, "mctest") != nullptr) isTest = true;
        }
        if (isTest) {
            LOGE("test build detected: native hooks disabled (offsets incomplete)");
            return JNI_VERSION_1_6;
        }
    }

    std::thread(waitForMinecraft).detach();
    return JNI_VERSION_1_6;
}
