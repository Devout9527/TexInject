#include "hook/InlineHook.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace texinject::hook
{
    namespace
    {

        constexpr uint32_t kLdrX16Pc8 = 0x58000050;
        constexpr uint32_t kBrX16 = 0xD61F0200;
        constexpr int kOverwriteBytes = 16;
        constexpr int kSavedBytes = 16;

        char gLastError[160] = { 0 };
        int gCount = 0;

        void Fail(const char* Reason)
        {
            std::snprintf(gLastError, sizeof(gLastError), "%s", Reason ? Reason : "未知错误");
        }

        bool MakeWritable(void* Address, std::size_t Size)
        {
            const long Page = sysconf(_SC_PAGESIZE);
            if (Page <= 0)
            {
                return false;
            }
            const uintptr_t Start = reinterpret_cast<uintptr_t>(Address) & ~static_cast<uintptr_t>(Page - 1);
            const uintptr_t End =
                (reinterpret_cast<uintptr_t>(Address) + Size + Page - 1) & ~static_cast<uintptr_t>(Page - 1);
            return mprotect(reinterpret_cast<void*>(Start), End - Start,
                            PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
        }

        bool IsRelocatable(const uint32_t* Code, int Count)
        {
            for (int Index = 0; Index < Count; ++Index)
            {
                const uint32_t Ins = Code[Index];
                if ((Ins & 0x9F000000) == 0x90000000) return false;
                if ((Ins & 0x9F000000) == 0x10000000) return false;
                if ((Ins & 0x3B000000) == 0x18000000) return false;
                if ((Ins & 0xFF000010) == 0x54000000) return false;
                if ((Ins & 0x7C000000) == 0x14000000) return false;
            }
            return true;
        }
    }

    const char* LastError() { return gLastError; }

    int Count() { return gCount; }

    bool Install(void* Target, void* Replace, void** Trampoline)
    {
        if (!Target || !Replace)
        {
            Fail("参数为空");
            return false;
        }
        auto* Code = static_cast<uint32_t*>(Target);

        if (!IsRelocatable(Code, kSavedBytes / 4))
        {
            Fail("函数头部含 PC 相对指令，搬不走");
            return false;
        }

        const long Page = sysconf(_SC_PAGESIZE);
        void* Mem = mmap(nullptr, static_cast<std::size_t>(Page),
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (Mem == MAP_FAILED)
        {
            Fail("mmap trampoline 失败");
            return false;
        }

        auto* Tramp = static_cast<uint32_t*>(Mem);
        std::memcpy(Tramp, Code, kSavedBytes);

        uint32_t* Tail = Tramp + (kSavedBytes / 4);
        Tail[0] = kLdrX16Pc8;
        Tail[1] = kBrX16;
        const uint64_t Back = reinterpret_cast<uint64_t>(Target) + kSavedBytes;
        std::memcpy(Tail + 2, &Back, sizeof(Back));

        if (!MakeWritable(Target, kOverwriteBytes))
        {
            Fail("目标页不可写（mprotect 失败）");
            munmap(Mem, static_cast<std::size_t>(Page));
            return false;
        }

        Code[0] = kLdrX16Pc8;
        Code[1] = kBrX16;
        const uint64_t To = reinterpret_cast<uint64_t>(Replace);
        std::memcpy(Code + 2, &To, sizeof(To));

        __builtin___clear_cache(reinterpret_cast<char*>(Target),
                                reinterpret_cast<char*>(Target) + kOverwriteBytes);

        if (Trampoline)
        {
            *Trampoline = Mem;
        }
        ++gCount;
        return true;
    }
}
