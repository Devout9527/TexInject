#include "game/LocalPlayerHook.h"

#include "hook/InlineHook.h"
#include "mem/MemRw.h"

#include <android/log.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#define LPLOG(...) __android_log_print(ANDROID_LOG_INFO, "TexInjectMem", __VA_ARGS__)

namespace texinject::game
{
    namespace
    {
        constexpr const char* kSoName = "libminecraftpe.so";

        struct VersionEntry
        {
            const char* Prefix;
            uintptr_t NormalTickRva;
        };

        // 只登记验过的版本，别猜 —— 地址错一个字节就是 SIGSEGV
        const VersionEntry kEntries[] = {
            { "3.9",  0x074c79fc },
            { "3.10", 0x0759b6c8 },
        };

        char gVersion[64] = { 0 };
        std::atomic<void*> gLocalPlayer{ nullptr };
        void (*gOrigNormalTick)(void*) = nullptr;
        bool gInstalled = false;

        void NormalTickProxy(void* Self)
        {
            if (Self)
            {
                gLocalPlayer.store(Self);
            }
            if (gOrigNormalTick)
            {
                gOrigNormalTick(Self);
            }
        }

        uintptr_t RvaForVersion()
        {
            for (const VersionEntry& Entry : kEntries)
            {
                const size_t Length = std::strlen(Entry.Prefix);
                if (std::strncmp(gVersion, Entry.Prefix, Length) == 0)
                {
                    return Entry.NormalTickRva;
                }
            }
            return 0;
        }
    }

    void SetGameVersion(const char* Version)
    {
        if (!Version)
        {
            return;
        }
        std::snprintf(gVersion, sizeof(gVersion), "%s", Version);
    }

    bool InstallLocalPlayerHook()
    {
        if (gInstalled)
        {
            return true;
        }
        const uintptr_t Base = mem::ModuleBase(kSoName);
        if (Base == 0)
        {
            LPLOG("安装失败：%s 未加载", kSoName);
            return false;
        }
        const uintptr_t Rva = RvaForVersion();
        if (Rva == 0)
        {
            LPLOG("安装失败：游戏版本 \"%s\" 没有 normalTick 地址（setGameVersion 调过了吗）", gVersion);
            return false;
        }
        void* Target = reinterpret_cast<void*>(Base + Rva);
        if (!mem::Readable(reinterpret_cast<uintptr_t>(Target)))
        {
            LPLOG("安装失败：0x%lx 不可读（地址表对不上这个 so）", static_cast<unsigned long>(Base + Rva));
            return false;
        }
        if (!hook::Install(Target, reinterpret_cast<void*>(&NormalTickProxy),
                           reinterpret_cast<void**>(&gOrigNormalTick)))
        {
            LPLOG("安装失败：%s", hook::LastError());
            return false;
        }
        gInstalled = true;
        LPLOG("LocalPlayer hook 已装：%s + 0x%lx", kSoName, static_cast<unsigned long>(Rva));
        return true;
    }

    void* LocalPlayer()
    {
        return gLocalPlayer.load();
    }

    bool HasLocalPlayer()
    {
        return gLocalPlayer.load() != nullptr;
    }
}
