#include "ResourceSourceHook.h"
#include "ElfImports.h"
#include <android/log.h>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <mutex>
#include <iterator>
#include <utility>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "TexInjectMount"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static_assert(sizeof(void*) == 8, "Android ARM64 only");
static_assert(sizeof(resource_mount::elf::Dynamic) == sizeof(Elf64_Dyn));
static_assert(sizeof(resource_mount::elf::Symbol) == sizeof(Elf64_Sym));
static_assert(sizeof(resource_mount::elf::Rel) == sizeof(Elf64_Rel));
static_assert(sizeof(resource_mount::elf::Rela) == sizeof(Elf64_Rela));
namespace resource_mount {
namespace {
Paths paths;
std::mutex control;
std::atomic<bool> configured{false}, installed{false};
uintptr_t installedBase = 0;
namespace imports = resource_mount::elf;

FILE* sourceFopen(const char*, const char*);
int sourceStat(const char*, struct stat*);
int sourceAccess(const char*, int);
DIR* sourceOpendir(const char*);
int sourceOpen(const char*, int, ...);
int sourceOpen2(const char*, int);
ssize_t sourceReadlink(const char*, char*, size_t);
int sourceMkdir(const char*, mode_t);
int sourceUnlink(const char*);
int sourceRmdir(const char*);
int sourceRename(const char*, const char*);

struct Entry {
    const char* name;
    uintptr_t replacement;
    std::atomic<uintptr_t> original{0};
    uintptr_t slot = 0;
    int protection = -1;
};
Entry entries[] = {
    {"stat", reinterpret_cast<uintptr_t>(sourceStat)},
    {"access", reinterpret_cast<uintptr_t>(sourceAccess)},
    {"opendir", reinterpret_cast<uintptr_t>(sourceOpendir)},
    {"open", reinterpret_cast<uintptr_t>(sourceOpen)},
    {"__open_2", reinterpret_cast<uintptr_t>(sourceOpen2)},
    {"readlink", reinterpret_cast<uintptr_t>(sourceReadlink)},
    {"mkdir", reinterpret_cast<uintptr_t>(sourceMkdir)},
    {"unlink", reinterpret_cast<uintptr_t>(sourceUnlink)},
    {"rmdir", reinterpret_cast<uintptr_t>(sourceRmdir)},
    {"rename", reinterpret_cast<uintptr_t>(sourceRename)},
    {"fopen", reinterpret_cast<uintptr_t>(sourceFopen)},
};
template<class Function> Function Original(size_t index) {
    return reinterpret_cast<Function>(entries[index].original.load(std::memory_order_acquire));
}
struct Request {
    std::array<char, kMaxPath> buffer;
    const char* path;
    const char* alias;  // 别名原路径：物理目录里没有时回退到它
    Route route;
    explicit Request(const char* input)
        : path(input), alias(input), route(MapPath(input, buffer)) {
        if (route == Route::Redirect) path = buffer.data();
        if (route == Route::Invalid) errno = EINVAL;
    }
    bool rejectWrite() const {
        if (route == Route::Original) return false;
        errno = route == Route::Invalid ? EINVAL : EROFS;
        return true;
    }
    // 增量覆盖：物理目录只放被改过的文件，其余仍读游戏自己的包。
    // 原项目要求宿主把整个包发布到物理目录，缺文件直接消失；这里改成回退。
    bool fallback() const { return route == Route::Redirect && errno == ENOENT; }
};
FILE* sourceFopen(const char* path, const char* mode) {
    Request r(path);
    if (r.route == Route::Invalid) return nullptr;
    // No decoder, key wait or cache. Mounted files are returned as-is.
    if (r.route == Route::Redirect &&
        (!mode || mode[0] != 'r' || std::strchr(mode, '+'))) {
        errno = EROFS;
        return nullptr;
    }
    const auto original = Original<FILE*(*)(const char*, const char*)>(10);
    FILE* file = original(r.path, mode);
    if (!file && r.fallback()) file = original(r.alias, mode);
    return file;
}
int sourceStat(const char* path, struct stat* out) {
    Request r(path);
    if (r.route == Route::Invalid) return -1;
    const auto original = Original<int(*)(const char*, struct stat*)>(0);
    int result = original(r.path, out);
    if (result == -1 && r.fallback()) result = original(r.alias, out);
    return result;
}
int sourceAccess(const char* path, int mode) {
    Request r(path);
    if (r.route == Route::Invalid || ((mode & W_OK) && r.rejectWrite())) return -1;
    const auto original = Original<int(*)(const char*, int)>(1);
    int result = original(r.path, mode);
    if (result == -1 && r.fallback()) result = original(r.alias, mode);
    return result;
}
DIR* sourceOpendir(const char* path) {
    Request r(path);
    if (r.route == Route::Invalid) return nullptr;
    // A genuine libc DIR*: ordinary readdir/closedir need no wrappers.
    const auto original = Original<DIR*(*)(const char*)>(2);
    DIR* dir = original(r.path);
    if (!dir && r.fallback()) dir = original(r.alias);
    return dir;
}
bool HasMode(int flags) {
    return (flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE;
}
bool Writes(int flags) {
    return (flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND)) ||
           (flags & O_TMPFILE) == O_TMPFILE;
}
int sourceOpen(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (HasMode(flags)) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    Request r(path);
    if (r.route == Route::Invalid || (Writes(flags) && r.rejectWrite())) return -1;
    const auto original = Original<int(*)(const char*, int, ...)>(3);
    int fd = HasMode(flags) ? original(r.path, flags, mode) : original(r.path, flags);
    if (fd == -1 && r.fallback())
        fd = HasMode(flags) ? original(r.alias, flags, mode) : original(r.alias, flags);
    return fd;
}
int sourceOpen2(const char* path, int flags) {
    Request r(path);
    if (r.route == Route::Invalid || (Writes(flags) && r.rejectWrite())) return -1;
    // Preserve libc fortify's missing-mode check; never invent a mode argument.
    const auto original = Original<int(*)(const char*, int)>(4);
    int fd = original(r.path, flags);
    if (fd == -1 && r.fallback()) fd = original(r.alias, flags);
    return fd;
}
ssize_t sourceReadlink(const char* path, char* out, size_t size) {
    Request r(path);
    if (r.route == Route::Invalid) return -1;
    const auto original = Original<ssize_t(*)(const char*, char*, size_t)>(5);
    ssize_t n = original(r.path, out, size);
    if (n == -1 && r.fallback()) n = original(r.alias, out, size);
    return n;
}
int sourceMkdir(const char* path, mode_t mode) {
    Request r(path);
    return r.rejectWrite() ? -1 : Original<int(*)(const char*, mode_t)>(6)(path, mode);
}
int sourceUnlink(const char* path) {
    Request r(path);
    return r.rejectWrite() ? -1 : Original<int(*)(const char*)>(7)(path);
}
int sourceRmdir(const char* path) {
    Request r(path);
    return r.rejectWrite() ? -1 : Original<int(*)(const char*)>(8)(path);
}
int sourceRename(const char* from, const char* to) {
    Request a(from), b(to);
    return a.rejectWrite() || b.rejectWrite() ? -1 : Original<int(*)(const char*, const char*)>(9)(from, to);
}

struct Find { uintptr_t base; imports::Image image{}; bool found = false; };
int FindModule(dl_phdr_info* info, size_t, void* raw) {
    auto& result = *static_cast<Find*>(raw);
    if (!info->dlpi_name || (result.base && info->dlpi_addr != result.base)) return 0;
    const char* name = std::strrchr(info->dlpi_name, '/');
    if (std::strcmp(name ? name + 1 : info->dlpi_name, "libminecraftpe.so")) return 0;
    auto& image = result.image;
    image.base = info->dlpi_addr;
    for (size_t i = 0; i < info->dlpi_phnum; ++i) {
        const auto& p = info->dlpi_phdr[i];
        if (p.p_type != PT_LOAD && p.p_type != PT_DYNAMIC) continue;
        if (p.p_vaddr > UINTPTR_MAX - image.base || p.p_memsz > UINTPTR_MAX - image.base - p.p_vaddr) return 1;
        if (p.p_type == PT_DYNAMIC) {
            image.dynamic = image.base + p.p_vaddr;
            image.dynamic_size = p.p_memsz;
        } else {
            if (image.count == std::size(image.loads)) return 1;
            image.loads[image.count++] = {image.base + p.p_vaddr, static_cast<size_t>(p.p_memsz), p.p_flags};
        }
    }
    result.found = image.dynamic && image.count;
    return 1;
}
int Protection(uintptr_t address) {
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return -1;
    char line[512];
    int protection = -1;
    while (std::fgets(line, sizeof(line), maps)) {
        unsigned long begin = 0, end = 0;
        char permissions[5]{};
        if (std::sscanf(line, "%lx-%lx %4s", &begin, &end, permissions) != 3 || address < begin || address >= end) continue;
        protection = (permissions[0] == 'r' ? PROT_READ : 0) |
                     (permissions[1] == 'w' ? PROT_WRITE : 0) |
                     (permissions[2] == 'x' ? PROT_EXEC : 0);
        break;
    }
    std::fclose(maps);
    return protection;
}
bool Exchange(Entry& e, uintptr_t expected, uintptr_t desired, long pageSize) {
    void* page = reinterpret_cast<void*>(e.slot - e.slot % pageSize);
    const bool writable = e.protection & PROT_WRITE;
    if (!writable && mprotect(page, pageSize, e.protection | PROT_WRITE)) return false;
    auto* slot = reinterpret_cast<uintptr_t*>(e.slot);
    bool changed = __atomic_compare_exchange_n(slot, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (!writable && mprotect(page, pageSize, e.protection)) {
        // Restore our exact exchange only; don't overwrite a competing owner.
        if (changed) __atomic_compare_exchange_n(slot, &desired, expected, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        (void)mprotect(page, pageSize, e.protection);
        changed = false;
    }
    return changed;
}
} // namespace

bool Configure(const std::string& supplied, const std::string& canonical,
               const std::string& aliasSuffix, const std::string& storagePath) {
    {
        std::lock_guard<std::mutex> lock(control);
        Paths candidate;
        if (!candidate.Initialize(supplied, canonical, aliasSuffix, storagePath)) return false;
        if (configured.load(std::memory_order_acquire))
            return candidate.aliases == paths.aliases && candidate.storage == paths.storage;
        paths = std::move(candidate);
        configured.store(true, std::memory_order_release);
    }
    return true;
}
bool IsInstalled() { return installed.load(std::memory_order_acquire); }
bool PendingInstallation() { return configured.load(std::memory_order_acquire) && !IsInstalled(); }
Route MapPath(const char* input, std::array<char, kMaxPath>& output) {
    if (!IsInstalled() || !configured.load(std::memory_order_acquire)) return Route::Original;
    return paths.Map(input, output);
}
std::string Status() {
    return !configured.load(std::memory_order_acquire) ? "not_configured" :
        IsInstalled() ? "mounted" : "waiting_for_file_imports";
}
bool Install(uintptr_t base) {
    std::lock_guard<std::mutex> lock(control);
    if (IsInstalled()) return !base || base == installedBase;
    if (!configured.load(std::memory_order_acquire)) return false;
    Find context{base};
    dl_iterate_phdr(FindModule, &context);
    if (!context.found) return false;
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;
    // Resolve every imported libc symbol before changing any slot. These are
    // third-party symbols with their public C ABI, not callable game RVAs.
    for (auto& e : entries) {
        const auto result = imports::FindSymbol(context.image, e.name, [](uintptr_t at, void* out, size_t count) {
            std::memcpy(out, reinterpret_cast<const void*>(at), count);
            return true;
        });
        if (result.status != imports::Status::Found) {
            LOGE("resource source import missing: %s status=%d", e.name, static_cast<int>(result.status));
            return false;
        }
        e.slot = result.slot;
        e.protection = Protection(e.slot);
        if (!(e.protection & PROT_READ) || e.protection < 0 || (e.protection & PROT_EXEC)) return false;
        const uintptr_t original = __atomic_load_n(reinterpret_cast<uintptr_t*>(e.slot), __ATOMIC_ACQUIRE);
        if (!original) return false;
        if (original != e.replacement) e.original.store(original, std::memory_order_release);
        else if (!e.original.load(std::memory_order_acquire)) return false;
    }
    size_t patched = 0;
    for (; patched < std::size(entries); ++patched) {
        auto& e = entries[patched];
        const auto value = __atomic_load_n(reinterpret_cast<uintptr_t*>(e.slot), __ATOMIC_ACQUIRE);
        if (value != e.replacement && !Exchange(e, e.original.load(std::memory_order_acquire), e.replacement, pageSize)) break;
    }
    if (patched != std::size(entries)) {
        while (patched) {
            auto& e = entries[--patched];
            (void)Exchange(e, e.replacement, e.original.load(std::memory_order_acquire), pageSize);
        }
        LOGE("resource source installation incomplete; mount remains inactive");
        return false;
    }
    installedBase = context.image.base;
    installed.store(true, std::memory_order_release);
    LOGI("custom resource source mounted; imports=%zu; alias retained as fallback", std::size(entries));
    return true;
}
} // namespace resource_mount
