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
    return *reinterpret_cast<const uint32_t*>(p) == 0x464c457f;
}

struct FindCtx { const char* name; uintptr_t base; };

int findModuleCb(dl_phdr_info* info, size_t, void* data) {
    auto* c = static_cast<FindCtx*>(data);
    if (!info->dlpi_name || !info->dlpi_name[0]) return 0;
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* leaf = slash ? slash + 1 : info->dlpi_name;
    if (std::strcmp(leaf, c->name) != 0) return 0;
    c->base = static_cast<uintptr_t>(info->dlpi_addr);
    return 1;
}

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

void ensureNativeHooks(JNIEnv*, jclass) { ensureBase(); }

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

void setGameVersion(JNIEnv* env, jclass, jstring jver) {
    if (!jver) return;
    const char* s = env->GetStringUTFChars(jver, nullptr);
    if (!s) return;
    FunctionsAddress::setVersion(s);
    env->ReleaseStringUTFChars(jver, s);
}

const JNINativeMethod native_methods[] = {
    {"ensureNativeHooks", "()V",     reinterpret_cast<void*>(ensureNativeHooks)},
    {"runPythonFile",     "(Ljava/lang/String;)Z", reinterpret_cast<void*>(runPythonFile)},
    {"runPythonSource",   "(Ljava/lang/String;)Z", reinterpret_cast<void*>(runPythonSource)},
    {"setGameVersion",    "(Ljava/lang/String;)V", reinterpret_cast<void*>(setGameVersion)},
};

}

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
