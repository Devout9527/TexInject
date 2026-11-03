/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 这是 TexInject 的一部分。
 * TexInject 是自由软件：你可以依据 GNU Affero 通用公共许可证（AGPL）第 3 版
 * 或（由你选择）任何更新版本重新发布和/或修改它。
 *
 * TexInject 分发时希望它有用，但不提供任何担保，甚至不提供适销性或
 * 特定用途适用性的默示担保。详见 GNU Affero 通用公共许可证。
 */
#include "trace/IoTrace.h"

#include "main.h"
#include "plt/PltHook.h"

#include <android/log.h>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "TexInjectIoTrace"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

/**
 * 环形缓冲：16384 条 × 344 字节 ≈ 5.6MB 静态 .bss。
 * 不 malloc —— hook 里不能分配，而且这些内存在游戏进程里几乎不会被回收。
 *
 * 上一版是 4096 × 172。实测一次「进世界」就写满并绕圈了（一份 trace 里
 * 57% 的记录都被截在 159 字节，2110 次 textures/blocks 读取全糊成一团），
 * 所以条数和单条长度都翻了。
 */
constexpr uint32_t kRingSize = 16384;
constexpr uint32_t kPathMax = 320;

enum Kind : uint16_t {
    K_OPENAT = 0,
    K_OPEN = 1,
    K_FOPEN = 2,
    K_STAT = 3,
    K_LSTAT = 4,
    K_ACCESS = 5,
    K_FACCESSAT = 6,
    K_OPENDIR = 7,
    K_MKDIR = 8,
    K_UNLINK = 9,
    K_RENAME = 10,
    K_RMDIR = 11,
    K_TRUNCATE = 12,
};

/** kind 的最高位：path 超长被截断。解析日志时记得 mask 掉。 */
constexpr uint16_t kTruncFlag = 0x8000;

struct Rec {
    uint32_t rva;        // 调用者在目标 so 里的偏移（0 = 不在游戏模块内）
    int32_t ret;         // 被 hook 函数的返回值
    uint16_t kind;       // Kind | kTruncFlag
    uint16_t pad;
    uint32_t aux;        // open / openat 的 flags，其余为 0
    char mode[8];        // fopen 的 mode（"r" / "w" / "r+b" ...），其余为 0
    char path[kPathMax];
};

Rec g_ring[kRingSize];
std::atomic<uint32_t> g_pos{0};
std::atomic<bool> g_on{false};

/** 防重入：万一 record 里间接又触发被 hook 的函数，直接放行不再记录。 */
thread_local int g_depth = 0;

char g_err[192] = {0};
/** start() 时拼好的状态串，dump 时原样写进头部，省得再猜哪些符号没接上。 */
char g_status[512] = {0};

int (*real_openat)(int, const char*, int, ...) = nullptr;
int (*real_openat64)(int, const char*, int, ...) = nullptr;
int (*real__openat)(int, const char*, int, int) = nullptr;
int (*real_open)(const char*, int, ...) = nullptr;
int (*real_open64)(const char*, int, ...) = nullptr;
FILE* (*real_fopen)(const char*, const char*) = nullptr;
FILE* (*real_fopen64)(const char*, const char*) = nullptr;
int (*real_stat)(const char*, struct stat*) = nullptr;
int (*real_lstat)(const char*, struct stat*) = nullptr;
int (*real_access)(const char*, int) = nullptr;
int (*real_faccessat)(int, const char*, int, int) = nullptr;
DIR* (*real_opendir)(const char*) = nullptr;
int (*real_mkdir)(const char*, mode_t) = nullptr;
int (*real_unlink)(const char*) = nullptr;
int (*real_rename)(const char*, const char*) = nullptr;
int (*real_rmdir)(const char*) = nullptr;
int (*real_truncate)(const char*, off_t) = nullptr;

void setErr(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
}

inline void record(Kind kind, const char* path, int ret, uint32_t aux, const char* mode,
                   void* caller) {
    if (!path) return;
    const uintptr_t base = Main::getBaseAddress();
    const uintptr_t c = reinterpret_cast<uintptr_t>(caller);
    uint32_t rva = 0;
    // 只在确实是游戏自己的代码时才记偏移，避免掉进 libc 的地址
    if (base != 0 && c > base && (c - base) < 0x40000000ull) {
        rva = static_cast<uint32_t>(c - base);
    }

    const uint32_t slot = g_pos.fetch_add(1, std::memory_order_relaxed) % kRingSize;
    Rec& r = g_ring[slot];
    r.rva = rva;
    r.ret = ret;
    r.kind = kind;
    r.pad = 0;
    r.aux = aux;
    r.mode[0] = 0;

    if (mode && *mode) {
        size_t m = std::strlen(mode);
        if (m >= sizeof(r.mode)) m = sizeof(r.mode) - 1;
        std::memcpy(r.mode, mode, m);
        r.mode[m] = 0;
    }

    size_t n = std::strlen(path);
    if (n >= kPathMax) {
        n = kPathMax - 1;
        r.kind = static_cast<uint16_t>(kind | kTruncFlag);
    }
    std::memcpy(r.path, path, n);
    r.path[n] = 0;
}

bool enabled() {
    return g_on.load(std::memory_order_relaxed);
}

// ------------------------------------------------------------------ redirect
// 把对 vanilla.mcp 的打开重定向到我们指定的文件。
// 目的是验证「游戏认不认明文 mcp」—— 不用去逆 MCPK 的加密。
static std::atomic<bool> g_redirectOn{ false };
static char g_redirectPath[512] = { 0 };

// 下面这套是「下载捕获」：游戏往 storge/stream 里写的时候，直接写到外部目录，
// 不用等下载完再去目录里翻。读也一起搬 —— 只搬写的话，游戏自己下次读就找不到。
static std::atomic<bool> g_mirrorOn{ false };
static char g_mirrorRoot[256] = { 0 };
static thread_local char g_mirrorBuf[1024];

/** 把 fullPath 的所有父目录建出来（外部存储上不建就是 ENOENT）。
 *
 *  这里必须走 real_mkdir：hook_mkdir 自己也要 redirect，而 redirect 又会进来，
 *  直接调 mkdir 会无限递归。 */
static void ensureParents(const char* fullPath) {
    char tmp[1024];
    std::snprintf(tmp, sizeof(tmp), "%s", fullPath);
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            if (real_mkdir) real_mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

/**
 * redirect 的实体：结果写进调用方给的缓冲区。
 *
 * rename 有两个路径参数，共用一个 thread_local 缓冲会变成 rename(a, a) ——
 * 分片全留在 temp 里，下载器一看正式目录里没有就停。所以拆出来。
 */
static const char* redirectInto(const char* path, char* buf, std::size_t size) {
    if (!path) return path;
    if (g_redirectOn.load(std::memory_order_relaxed)) {
        const char* slash = std::strrchr(path, '/');
        const char* name = slash ? slash + 1 : path;
        if (std::strcmp(name, "vanilla.mcp") == 0) return g_redirectPath;
    }
    if (g_mirrorOn.load(std::memory_order_relaxed)) {
        static const char kMarker[] = "/games/com.netease/storge/stream/";
        const char* hit = std::strstr(path, kMarker);
        if (hit) {
            const char* rel = hit + sizeof(kMarker) - 1;
            std::snprintf(buf, size, "%s/%s", g_mirrorRoot, rel);
            ensureParents(buf);
            return buf;
        }
    }
    return path;
}

static const char* redirect(const char* path) {
    return redirectInto(path, g_mirrorBuf, sizeof(g_mirrorBuf));
}

// ------------------------------------------------------------------ hooks

int hook_openat(int dirfd, const char* path, int flags, ...) __attribute__((noinline));
int hook_openat(int dirfd, const char* path, int flags, ...) {
    path = redirect(path);
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    if (!real_openat) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_openat(dirfd, path, flags, mode);

    ++g_depth;
    const int r = real_openat(dirfd, path, flags, mode);
    --g_depth;
    record(K_OPENAT, path, r, static_cast<uint32_t>(flags), nullptr,
           __builtin_return_address(0));
    return r;
}

int hook_openat64(int dirfd, const char* path, int flags, ...) __attribute__((noinline));
int hook_openat64(int dirfd, const char* path, int flags, ...) {
    path = redirect(path);
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    if (!real_openat64) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_openat64(dirfd, path, flags, mode);

    ++g_depth;
    const int r = real_openat64(dirfd, path, flags, mode);
    --g_depth;
    record(K_OPENAT, path, r, static_cast<uint32_t>(flags), nullptr,
           __builtin_return_address(0));
    return r;
}

/** bionic 的 __openat 是 fortify 版：mode 恒定位传，不是可变参数。 */
int hook__openat(int dirfd, const char* path, int flags, int mode) __attribute__((noinline));
int hook__openat(int dirfd, const char* path, int flags, int mode) {
    path = redirect(path);
    if (!real__openat) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real__openat(dirfd, path, flags, mode);

    ++g_depth;
    const int r = real__openat(dirfd, path, flags, mode);
    --g_depth;
    record(K_OPENAT, path, r, static_cast<uint32_t>(flags), nullptr,
           __builtin_return_address(0));
    return r;
}

int hook_open(const char* path, int flags, ...) __attribute__((noinline));
int hook_open(const char* path, int flags, ...) {
    path = redirect(path);
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    if (!real_open) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_open(path, flags, mode);

    ++g_depth;
    const int r = real_open(path, flags, mode);
    --g_depth;
    record(K_OPEN, path, r, static_cast<uint32_t>(flags), nullptr,
           __builtin_return_address(0));
    return r;
}

int hook_open64(const char* path, int flags, ...) __attribute__((noinline));
int hook_open64(const char* path, int flags, ...) {
    path = redirect(path);
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    if (!real_open64) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_open64(path, flags, mode);

    ++g_depth;
    const int r = real_open64(path, flags, mode);
    --g_depth;
    record(K_OPEN, path, r, static_cast<uint32_t>(flags), nullptr,
           __builtin_return_address(0));
    return r;
}

FILE* hook_fopen(const char* path, const char* mode) __attribute__((noinline));
FILE* hook_fopen(const char* path, const char* mode) {
    path = redirect(path);
    if (!real_fopen) {
        errno = ENOSYS;
        return nullptr;
    }
    if (!enabled() || g_depth) return real_fopen(path, mode);

    ++g_depth;
    FILE* f = real_fopen(path, mode);
    --g_depth;
    record(K_FOPEN, path, f ? 0 : -1, 0, mode, __builtin_return_address(0));
    return f;
}

FILE* hook_fopen64(const char* path, const char* mode) __attribute__((noinline));
FILE* hook_fopen64(const char* path, const char* mode) {
    path = redirect(path);
    if (!real_fopen64) {
        errno = ENOSYS;
        return nullptr;
    }
    if (!enabled() || g_depth) return real_fopen64(path, mode);

    ++g_depth;
    FILE* f = real_fopen64(path, mode);
    --g_depth;
    record(K_FOPEN, path, f ? 0 : -1, 0, mode, __builtin_return_address(0));
    return f;
}

int hook_stat(const char* path, struct stat* buf) __attribute__((noinline));
int hook_stat(const char* path, struct stat* buf) {
    path = redirect(path);
    if (!real_stat) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_stat(path, buf);

    ++g_depth;
    const int r = real_stat(path, buf);
    --g_depth;
    record(K_STAT, path, r, 0, nullptr, __builtin_return_address(0));
    return r;
}

int hook_lstat(const char* path, struct stat* buf) __attribute__((noinline));
int hook_lstat(const char* path, struct stat* buf) {
    path = redirect(path);
    if (!real_lstat) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_lstat(path, buf);

    ++g_depth;
    const int r = real_lstat(path, buf);
    --g_depth;
    record(K_LSTAT, path, r, 0, nullptr, __builtin_return_address(0));
    return r;
}

int hook_access(const char* path, int mode) __attribute__((noinline));
int hook_access(const char* path, int mode) {
    path = redirect(path);
    if (!real_access) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_access(path, mode);

    ++g_depth;
    const int r = real_access(path, mode);
    --g_depth;
    record(K_ACCESS, path, r, static_cast<uint32_t>(mode), nullptr,
           __builtin_return_address(0));
    return r;
}

int hook_faccessat(int dirfd, const char* path, int mode, int flags) __attribute__((noinline));
int hook_faccessat(int dirfd, const char* path, int mode, int flags) {
    path = redirect(path);
    if (!real_faccessat) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_faccessat(dirfd, path, mode, flags);

    ++g_depth;
    const int r = real_faccessat(dirfd, path, mode, flags);
    --g_depth;
    record(K_FACCESSAT, path, r, static_cast<uint32_t>(mode), nullptr,
           __builtin_return_address(0));
    return r;
}

DIR* hook_opendir(const char* path) __attribute__((noinline));
DIR* hook_opendir(const char* path) {
    path = redirect(path);
    if (!real_opendir) {
        errno = ENOSYS;
        return nullptr;
    }
    if (!enabled() || g_depth) return real_opendir(path);

    ++g_depth;
    DIR* d = real_opendir(path);
    --g_depth;
    record(K_OPENDIR, path, d ? 0 : -1, 0, nullptr, __builtin_return_address(0));
    return d;
}

int hook_mkdir(const char* path, mode_t mode) __attribute__((noinline));
int hook_mkdir(const char* path, mode_t mode) {
    path = redirect(path);
    if (!real_mkdir) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_mkdir(path, mode);

    ++g_depth;
    const int r = real_mkdir(path, mode);
    --g_depth;
    record(K_MKDIR, path, r, static_cast<uint32_t>(mode), nullptr,
           __builtin_return_address(0));
    return r;
}

int hook_unlink(const char* path) __attribute__((noinline));
int hook_unlink(const char* path) {
    path = redirect(path);
    if (!real_unlink) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_unlink(path);

    ++g_depth;
    const int r = real_unlink(path);
    --g_depth;
    record(K_UNLINK, path, r, 0, nullptr, __builtin_return_address(0));
    return r;
}

int hook_rmdir(const char* path) __attribute__((noinline));
int hook_rmdir(const char* path) {
    path = redirect(path);
    if (!real_rmdir) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_rmdir(path);

    ++g_depth;
    const int r = real_rmdir(path);
    --g_depth;
    record(K_RMDIR, path, r, 0, nullptr, __builtin_return_address(0));
    return r;
}

int hook_truncate(const char* path, off_t len) __attribute__((noinline));
int hook_truncate(const char* path, off_t len) {
    path = redirect(path);
    if (!real_truncate) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled() || g_depth) return real_truncate(path, len);

    ++g_depth;
    const int r = real_truncate(path, len);
    --g_depth;
    record(K_TRUNCATE, path, r, static_cast<uint32_t>(len), nullptr,
           __builtin_return_address(0));
    return r;
}

int hook_rename(const char* from, const char* to) __attribute__((noinline));
int hook_rename(const char* from, const char* to) {
    if (!real_rename) {
        errno = ENOSYS;
        return -1;
    }
    // rename 有两个路径，不能共用 redirect 那个 thread_local 缓冲 ——
    // 会变成 rename(a, a)，分片全烂在 temp 里。
    char fromBuf[1024];
    char toBuf[1024];
    const char* newFrom = from ? redirectInto(from, fromBuf, sizeof(fromBuf)) : from;
    const char* newTo = to ? redirectInto(to, toBuf, sizeof(toBuf)) : to;
    if (!enabled() || g_depth) return real_rename(newFrom, newTo);

    ++g_depth;
    const int r = real_rename(newFrom, newTo);
    --g_depth;
    // 两个路径拼成 "旧 -> 新"，够用了；超长由 record 统一截断
    if (newFrom && newTo) {
        char buf[kPathMax * 2 + 8];
        std::snprintf(buf, sizeof(buf), "%s -> %s", newFrom, newTo);
        record(K_RENAME, buf, r, 0, nullptr, __builtin_return_address(0));
    }
    return r;
}

// ------------------------------------------------------------------ 安装表

struct Target {
    const char* name;
    void* hook;
    void** real;
};

const Target kTargets[] = {
        {"openat", reinterpret_cast<void*>(hook_openat),
         reinterpret_cast<void**>(&real_openat)},
        {"openat64", reinterpret_cast<void*>(hook_openat64),
         reinterpret_cast<void**>(&real_openat64)},
        {"__openat", reinterpret_cast<void*>(hook__openat),
         reinterpret_cast<void**>(&real__openat)},
        {"open", reinterpret_cast<void*>(hook_open), reinterpret_cast<void**>(&real_open)},
        {"open64", reinterpret_cast<void*>(hook_open64),
         reinterpret_cast<void**>(&real_open64)},
        {"fopen", reinterpret_cast<void*>(hook_fopen),
         reinterpret_cast<void**>(&real_fopen)},
        {"fopen64", reinterpret_cast<void*>(hook_fopen64),
         reinterpret_cast<void**>(&real_fopen64)},
        {"stat", reinterpret_cast<void*>(hook_stat), reinterpret_cast<void**>(&real_stat)},
        {"lstat", reinterpret_cast<void*>(hook_lstat),
         reinterpret_cast<void**>(&real_lstat)},
        {"access", reinterpret_cast<void*>(hook_access),
         reinterpret_cast<void**>(&real_access)},
        {"faccessat", reinterpret_cast<void*>(hook_faccessat),
         reinterpret_cast<void**>(&real_faccessat)},
        {"opendir", reinterpret_cast<void*>(hook_opendir),
         reinterpret_cast<void**>(&real_opendir)},
        {"mkdir", reinterpret_cast<void*>(hook_mkdir),
         reinterpret_cast<void**>(&real_mkdir)},
        {"unlink", reinterpret_cast<void*>(hook_unlink),
         reinterpret_cast<void**>(&real_unlink)},
        {"rename", reinterpret_cast<void*>(hook_rename),
         reinterpret_cast<void**>(&real_rename)},
        {"rmdir", reinterpret_cast<void*>(hook_rmdir),
         reinterpret_cast<void**>(&real_rmdir)},
        {"truncate", reinterpret_cast<void*>(hook_truncate),
         reinterpret_cast<void**>(&real_truncate)},
};

constexpr size_t kTargetCount = sizeof(kTargets) / sizeof(kTargets[0]);
bool g_installed[kTargetCount] = {false};

void buildStatus() {
    g_status[0] = 0;
    std::strncat(g_status, "hooked=", sizeof(g_status) - std::strlen(g_status) - 1);
    int hooked = 0, missing = 0;
    for (size_t i = 0; i < kTargetCount; ++i) {
        if (!g_installed[i]) continue;
        std::strncat(g_status, kTargets[i].name,
                     sizeof(g_status) - std::strlen(g_status) - 2);
        std::strncat(g_status, " ", sizeof(g_status) - std::strlen(g_status) - 1);
        ++hooked;
    }
    std::strncat(g_status, "| missing=", sizeof(g_status) - std::strlen(g_status) - 1);
    for (size_t i = 0; i < kTargetCount; ++i) {
        if (g_installed[i]) continue;
        std::strncat(g_status, kTargets[i].name,
                     sizeof(g_status) - std::strlen(g_status) - 2);
        std::strncat(g_status, " ", sizeof(g_status) - std::strlen(g_status) - 1);
        ++missing;
    }
    char tail[96];
    std::snprintf(tail, sizeof(tail), "| %d/%zu imports=%d", hooked, kTargetCount,
                  Plt::importCount());
    std::strncat(g_status, tail, sizeof(g_status) - std::strlen(g_status) - 1);
}

}  // namespace

namespace IoTrace {

bool start() {
    if (enabled()) return true;

    if (!Plt::init()) {
        setErr("plt init failed: %s", Plt::lastError());
        LOGE("%s", g_err);
        return false;
    }

    for (size_t i = 0; i < kTargetCount; ++i) {
        void* real = Plt::replace(kTargets[i].name, kTargets[i].hook);
        if (real) {
            *kTargets[i].real = real;
            g_installed[i] = true;
        } else {
            g_installed[i] = false;
        }
    }

    // 没有 openat / fopen 之一就等于没追踪，说明目标模块的导入表跟我们
    // 预期的不一样，报错比静默成功更有用。已经把接上的就地摘回去，
    // 别留一半 hook 在那儿。
    if (!real_openat && !real_fopen) {
        for (size_t i = 0; i < kTargetCount; ++i) {
            if (g_installed[i]) Plt::restore(kTargets[i].name);
        }
        setErr("openat/fopen not hooked (imports=%d)", Plt::importCount());
        LOGE("%s", g_err);
        return false;
    }

    g_pos.store(0, std::memory_order_relaxed);
    std::memset(g_ring, 0, sizeof(g_ring));
    buildStatus();
    g_on.store(true, std::memory_order_release);

    LOGI("io trace started: %s", g_status);
    return true;
}

void stop() {
    // 先关记录，再还原 GOT，避免还原过程中的调用还往缓冲里写
    g_on.store(false, std::memory_order_release);
    for (size_t i = 0; i < kTargetCount; ++i) {
        if (g_installed[i]) Plt::restore(kTargets[i].name);
    }
    LOGI("io trace stopped");
}

/**
 * 装 hook 并把对 vanilla.mcp 的打开重定向到 toPath。
 *
 * 用来验证游戏认不认明文 mcp：把测试文件放好，重启，看日志里
 * `Init use mcp ok` 还在不在、或者报什么错。只重定向、不记录，
 * 所以 g_on 一直是 false，不会有额外开销。
 */
/**
 * 把 storge/stream 整个镜像到 root 下（读写都改到那里）。
 *
 * root 一般是 /storage/emulated/0/Android/data/<pkg>/files/pack_netease/Download，
 * 这样游戏一下载完，文件就已经在外部可见的位置了。
 */
bool installMirror(const char* root) {
    if (!root || !root[0]) return false;
    std::snprintf(g_mirrorRoot, sizeof(g_mirrorRoot), "%s", root);
    ensureParents(g_mirrorRoot);
    mkdir(g_mirrorRoot, 0777);
    if (!start()) return false;
    g_on.store(false, std::memory_order_release);
    g_mirrorOn.store(true, std::memory_order_release);
    LOGI("stream mirror: %s", g_mirrorRoot);
    return true;
}

bool installRedirect(const char* toPath) {
    if (!toPath || !toPath[0]) return false;
    std::snprintf(g_redirectPath, sizeof(g_redirectPath), "%s", toPath);
    if (!start()) return false;
    g_on.store(false, std::memory_order_release);
    g_redirectOn.store(true, std::memory_order_release);
    LOGI("mcp redirect: *vanilla.mcp -> %s", g_redirectPath);
    return true;
}

bool running() {
    return enabled();
}

size_t count() {
    const uint32_t n = g_pos.load(std::memory_order_relaxed);
    return n < kRingSize ? n : kRingSize;
}

bool dump(const char* path) {
    if (!path || !*path) return false;

    // 用原函数指针写文件，别绕回被 hook 的 GOT
    FILE* f = real_fopen ? real_fopen(path, "we") : fopen(path, "we");
    if (!f) {
        setErr("cannot open %s: %s", path, strerror(errno));
        LOGE("%s", g_err);
        return false;
    }

    const uint32_t produced = g_pos.load(std::memory_order_relaxed);
    const uint32_t total = produced < kRingSize ? produced : kRingSize;
    const uint32_t startPos = produced < kRingSize ? 0 : (produced % kRingSize);

    std::fprintf(f, "# TexInject io trace\n");
    std::fprintf(f, "# base=0x%lx running=%d entries=%u ring=%u pathmax=%u\n",
                 static_cast<unsigned long>(Main::getBaseAddress()), enabled() ? 1 : 0, total,
                 kRingSize, kPathMax);
    std::fprintf(f, "# %s\n", g_status[0] ? g_status : "status=?");
    std::fprintf(f, "# kind: 0=openat 1=open 2=fopen 3=stat 4=lstat 5=access "
                    "6=faccessat 7=opendir 8=mkdir 9=unlink 10=rename 11=rmdir "
                    "12=truncate\n");
    std::fprintf(f, "# rva,kind,ret,aux,mode,path   (kind 含 0x8000 = path 被截断)\n");
    for (uint32_t i = 0; i < total; ++i) {
        const Rec& r = g_ring[(startPos + i) % kRingSize];
        if (r.path[0] == 0) continue;
        std::fprintf(f, "0x%08x,%u,%d,%u,%s,%s\n", static_cast<unsigned>(r.rva),
                     static_cast<unsigned>(r.kind), static_cast<int>(r.ret),
                     static_cast<unsigned>(r.aux), r.mode[0] ? r.mode : "-", r.path);
    }
    std::fclose(f);

    LOGI("io trace dumped: %s (%u entries)", path, total);
    return true;
}

const char* lastError() {
    return g_err;
}

}  // namespace IoTrace
