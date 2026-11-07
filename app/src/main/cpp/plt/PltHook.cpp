#include "PltHook.h"

#include <android/log.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>

#define LOG_TAG "TexInjectPlt"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef R_AARCH64_GLOB_DAT
#define R_AARCH64_GLOB_DAT 1025
#endif
#ifndef R_AARCH64_JUMP_SLOT
#define R_AARCH64_JUMP_SLOT 1026
#endif

namespace {

constexpr const char* kTargetModule = "libminecraftpe.so";

std::mutex g_lock;
bool g_tried = false;
bool g_ok = false;
uintptr_t g_base = 0;
char g_err[192] = {0};
std::vector<Plt::Entry> g_entries;

void setErr(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
}

inline uintptr_t fixup(uintptr_t v, uintptr_t base) {
    if (v == 0) return 0;
    return v < base ? v + base : v;
}

int protOf(uintptr_t addr) {
    FILE* f = fopen("/proc/self/maps", "re");
    if (!f) return -1;
    char line[512];
    int prot = -1;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long lo = 0, hi = 0;
        char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
        if (addr >= lo && addr < hi) {
            prot = 0;
            if (perms[0] == 'r') prot |= PROT_READ;
            if (perms[1] == 'w') prot |= PROT_WRITE;
            if (perms[2] == 'x') prot |= PROT_EXEC;
            break;
        }
    }
    fclose(f);
    return prot;
}

bool writeSlot(void** slot, void* value) {
    const uintptr_t pageSize = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    const uintptr_t addr = reinterpret_cast<uintptr_t>(slot);
    const uintptr_t pageAddr = addr & ~(pageSize - 1);

    const int oldProt = protOf(addr);
    if (mprotect(reinterpret_cast<void*>(pageAddr), pageSize,
                 PROT_READ | PROT_WRITE) != 0) {
        setErr("mprotect(RW) @0x%lx failed: %s",
               static_cast<unsigned long>(pageAddr), strerror(errno));
        return false;
    }

    *slot = value;
    __builtin___clear_cache(reinterpret_cast<char*>(slot),
                            reinterpret_cast<char*>(slot) + sizeof(void*));

    if (oldProt > 0) {
        mprotect(reinterpret_cast<void*>(pageAddr), pageSize, oldProt);
    }
    return true;
}

struct PhdrCtx {
    const char* name;
    uintptr_t base;
    const ElfW(Phdr)* phdr;
    int phnum;
};

int phdrCb(dl_phdr_info* info, size_t, void* data) {
    auto* c = static_cast<PhdrCtx*>(data);
    if (!info->dlpi_name || !info->dlpi_name[0]) return 0;
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* leaf = slash ? slash + 1 : info->dlpi_name;
    if (std::strcmp(leaf, c->name) != 0) return 0;
    c->base = static_cast<uintptr_t>(info->dlpi_addr);
    c->phdr = info->dlpi_phdr;
    c->phnum = info->dlpi_phnum;
    return 1;
}

bool parseRelocs(uintptr_t base, const ElfW(Phdr)* ph, int n,
                 std::vector<Plt::Entry>& out) {
    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < n; ++i) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const ElfW(Dyn)*>(base + ph[i].p_vaddr);
            break;
        }
    }
    if (!dyn) {
        setErr("no PT_DYNAMIC");
        return false;
    }

    uintptr_t jmprel = 0, rela = 0, symtab = 0, strtab = 0;
    size_t jmprelsz = 0, relasz = 0;
    for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_JMPREL:   jmprel = d->d_un.d_ptr; break;
            case DT_PLTRELSZ: jmprelsz = d->d_un.d_val; break;
            case DT_RELA:     rela = d->d_un.d_ptr; break;
            case DT_RELASZ:   relasz = d->d_un.d_val; break;
            case DT_SYMTAB:   symtab = d->d_un.d_ptr; break;
            case DT_STRTAB:   strtab = d->d_un.d_ptr; break;
            default: break;
        }
    }

    const auto* syms = reinterpret_cast<const ElfW(Sym)*>(fixup(symtab, base));
    const char* strs = reinterpret_cast<const char*>(fixup(strtab, base));
    if (!syms || !strs) {
        setErr("missing DT_SYMTAB / DT_STRTAB");
        return false;
    }

    int unresolvable = 0;

    auto take = [&](const ElfW(Rela)* r, uint32_t want) {
        const uint32_t type = static_cast<uint32_t>(ELF64_R_TYPE(r->r_info));
        if (type != want) return;
        const uint32_t si = static_cast<uint32_t>(ELF64_R_SYM(r->r_info));
        if (si == 0) return;
        const char* nm = strs + syms[si].st_name;
        if (!nm || !*nm) {
            ++unresolvable;
            return;
        }

        out.push_back(Plt::Entry{nm, reinterpret_cast<void**>(base + r->r_offset), nullptr});
    };

    if (jmprel && jmprelsz) {
        const auto* p = reinterpret_cast<const ElfW(Rela)*>(fixup(jmprel, base));
        const size_t cnt = jmprelsz / sizeof(ElfW(Rela));
        for (size_t i = 0; i < cnt; ++i) take(&p[i], R_AARCH64_JUMP_SLOT);
    }
    if (rela && relasz) {
        const auto* p = reinterpret_cast<const ElfW(Rela)*>(fixup(rela, base));
        const size_t cnt = relasz / sizeof(ElfW(Rela));
        for (size_t i = 0; i < cnt; ++i) take(&p[i], R_AARCH64_GLOB_DAT);
    }

    if (unresolvable > 0) {
        LOGI("skip %d reloc(s) with empty symbol name", unresolvable);
    }
    return true;
}

}

namespace Plt {

bool init() {
    {
        std::lock_guard<std::mutex> lk(g_lock);
        if (g_tried) return g_ok;
        g_tried = true;
        g_ok = false;
    }

    PhdrCtx ctx{kTargetModule, 0, nullptr, 0};
    dl_iterate_phdr(phdrCb, &ctx);

    std::lock_guard<std::mutex> lk(g_lock);
    if (ctx.base == 0 || ctx.phdr == nullptr) {
        setErr("%s not loaded yet", kTargetModule);
        g_tried = false;
        return false;
    }

    g_base = ctx.base;
    g_entries.clear();
    g_entries.reserve(4096);

    if (!parseRelocs(ctx.base, ctx.phdr, ctx.phnum, g_entries)) {
        g_tried = false;
        return false;
    }

    g_ok = !g_entries.empty();
    if (!g_ok) {
        setErr("no import slots parsed");
        g_tried = false;
        return false;
    }
    LOGI("plt ready: base=0x%lx imports=%zu",
         static_cast<unsigned long>(g_base), g_entries.size());
    return true;
}

bool ready() {
    std::lock_guard<std::mutex> lk(g_lock);
    return g_ok;
}

int importCount() {
    std::lock_guard<std::mutex> lk(g_lock);
    return static_cast<int>(g_entries.size());
}

void** find(const char* symbol) {
    if (!symbol) return nullptr;
    if (!init()) return nullptr;
    std::lock_guard<std::mutex> lk(g_lock);
    for (auto& e : g_entries) {
        if (e.name == symbol) return e.slot;
    }
    return nullptr;
}

void* replace(const char* symbol, void* replacement) {
    if (!symbol || !replacement) return nullptr;
    if (!init()) return nullptr;

    std::lock_guard<std::mutex> lk(g_lock);
    for (auto& e : g_entries) {
        if (e.name != symbol) continue;

        void* real = *e.slot;
        if (real == replacement) real = nullptr;
        if (!real) real = dlsym(RTLD_DEFAULT, symbol);
        if (!real) {
            setErr("cannot resolve %s", symbol);
            return nullptr;
        }
        e.orig = real;

        if (!writeSlot(e.slot, replacement)) return nullptr;
        LOGI("hooked %s: got=0x%lx -> 0x%lx",
             symbol, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(real)),
             static_cast<unsigned long>(reinterpret_cast<uintptr_t>(replacement)));
        return real;
    }

    setErr("symbol not imported: %s", symbol);
    return nullptr;
}

bool restore(const char* symbol) {
    if (!symbol) return false;
    if (!init()) return false;

    std::lock_guard<std::mutex> lk(g_lock);
    for (auto& e : g_entries) {
        if (e.name != symbol) continue;
        if (!e.orig) return false;
        if (!writeSlot(e.slot, e.orig)) return false;
        LOGI("restored %s", symbol);
        return true;
    }
    return false;
}

const char* lastError() {
    std::lock_guard<std::mutex> lk(g_lock);
    return g_err;
}

std::vector<std::string> allNames() {
    std::vector<std::string> out;
    if (!init()) return out;
    std::lock_guard<std::mutex> lk(g_lock);
    out.reserve(g_entries.size());
    for (auto& e : g_entries) out.push_back(e.name);
    return out;
}

}
