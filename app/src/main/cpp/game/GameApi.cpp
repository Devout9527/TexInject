#include "game/GameApi.h"

#include "game/LocalPlayerHook.h"
#include "hook/InlineHook.h"
#include "mem/MemRw.h"

#include <android/log.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#define GLOG(...) __android_log_print(ANDROID_LOG_INFO, "TexInjectGame", __VA_ARGS__)

namespace texinject::game
{
    namespace
    {

        constexpr uintptr_t kCameraAvoidanceTick = 136596916;

        constexpr uintptr_t kPlayerCarriedItem = 220033052;
        constexpr uintptr_t kItemStackAux = 32;
        constexpr uintptr_t kItemStackUserData = 16;

        constexpr uintptr_t kCompoundCtor = 171858952;
        constexpr uintptr_t kPutByte = 171865796;
        constexpr uintptr_t kPutCompound = 171867332;
        constexpr uintptr_t kPutDouble = 171866704;
        constexpr uintptr_t kPutFloat = 171866500;
        constexpr uintptr_t kPutInt = 171866148;
        constexpr uintptr_t kPutInt64 = 171866324;
        constexpr uintptr_t kPutShort = 171865972;
        constexpr uintptr_t kPutString = 171866908;

        constexpr std::size_t kTagSize = 0x80;

        constexpr uintptr_t kActorInit = 216535188;
        constexpr uintptr_t kActorPosPtr = 648;
        constexpr uintptr_t kPlayerGameMode = 5096;
        constexpr uintptr_t kActorGetHealth = 216639472;
        constexpr uintptr_t kKillAuraAttack = 218548440;

        uintptr_t gBase = 0;
        std::atomic<bool> gVersionOk{ false };
        std::atomic<bool> gCameraClip{ false };
        bool gCameraHooked = false;

        uintptr_t Addr()
        {
            if (gBase == 0)
            {
                return 0;
            }
            const uintptr_t Target = gBase + kCameraAvoidanceTick;
            return mem::Readable(Target) ? Target : 0;
        }

        constexpr int kMaxEntities = 512;
        void* gEntities[kMaxEntities] = { nullptr };
        std::atomic<int> gEntityCount{ 0 };
        std::atomic<bool> gKillAura{ false };
        bool gActorHooked = false;
        void (*gOrigActorInit)(void*) = nullptr;
        int gAttackCooldown = 0;
        std::atomic<bool> gTickerStarted{ false };

        void ActorInitProxy(void* Self)
        {
            if (gOrigActorInit)
            {
                gOrigActorInit(Self);
            }
            if (!Self)
            {
                return;
            }
            const int Slot = gEntityCount.load();
            if (Slot >= kMaxEntities)
            {
                return;
            }
            for (int Index = 0; Index < Slot; ++Index)
            {
                if (gEntities[Index] == Self)
                {
                    return;
                }
            }
            gEntities[Slot] = Self;
            gEntityCount.store(Slot + 1);
        }

        bool InstallActorHook()
        {
            if (gActorHooked)
            {
                return true;
            }
            if (gBase == 0)
            {
                return false;
            }
            void* Target = reinterpret_cast<void*>(gBase + kActorInit);
            if (!mem::Readable(reinterpret_cast<uintptr_t>(Target)))
            {
                GLOG("杀戮光环：Actor::init 地址读不到");
                return false;
            }
            if (!hook::Install(Target, reinterpret_cast<void*>(&ActorInitProxy),
                               reinterpret_cast<void**>(&gOrigActorInit)))
            {
                GLOG("杀戮光环：hook Actor::init 失败 %s", hook::LastError());
                return false;
            }
            gActorHooked = true;
            GLOG("杀戮光环：Actor::init hook 已装");
            return true;
        }

        bool ActorPosition(void* Actor, float& X, float& Y, float& Z)
        {
            uintptr_t PosPtr = 0;
            if (!Actor || !mem::ReadPtr(reinterpret_cast<uintptr_t>(Actor) + kActorPosPtr, PosPtr) ||
                PosPtr == 0)
            {
                return false;
            }
            float Values[3] = { 0.0f, 0.0f, 0.0f };
            if (!mem::ReadRaw(PosPtr, Values, sizeof(Values)))
            {
                return false;
            }
            X = Values[0];
            Y = Values[1];
            Z = Values[2];
            return std::fabs(X) < 30000000.0f && std::fabs(Z) < 30000000.0f;
        }

        float ActorHealth(void* Actor)
        {
            if (gBase == 0)
            {
                return 0.0f;
            }
            auto Fn = reinterpret_cast<float (*)(void*)>(gBase + kActorGetHealth);
            if (!mem::Readable(reinterpret_cast<uintptr_t>(Fn)))
            {
                return 0.0f;
            }
            return Fn(Actor);
        }

        void TickKillAura(void* Player)
        {
            if (!gKillAura.load() || gBase == 0)
            {
                return;
            }
            if (gAttackCooldown > 0)
            {
                --gAttackCooldown;
                return;
            }
            gAttackCooldown = 2;

            uintptr_t GameMode = 0;
            if (!mem::ReadPtr(reinterpret_cast<uintptr_t>(Player) + kPlayerGameMode, GameMode) ||
                GameMode == 0)
            {
                return;
            }
            float PX = 0.0f, PY = 0.0f, PZ = 0.0f;
            if (!ActorPosition(Player, PX, PY, PZ))
            {
                return;
            }
            auto Attack = reinterpret_cast<void (*)(void*, void*)>(gBase + kKillAuraAttack);
            if (!mem::Readable(reinterpret_cast<uintptr_t>(Attack)))
            {
                return;
            }

            const int Count = gEntityCount.load();
            int Alive = 0;
            void* Best = nullptr;
            float BestDistance = 3.5f;
            for (int Index = 0; Index < Count; ++Index)
            {
                void* Entity = gEntities[Index];
                if (!Entity || Entity == Player)
                {
                    continue;
                }

                uintptr_t VTable = 0;
                if (!mem::ReadPtr(reinterpret_cast<uintptr_t>(Entity), VTable) || VTable == 0)
                {
                    gEntities[Index] = nullptr;
                    continue;
                }
                ++Alive;
                float EX = 0.0f, EY = 0.0f, EZ = 0.0f;
                if (!ActorPosition(Entity, EX, EY, EZ))
                {
                    continue;
                }
                const float DX = EX - PX;
                const float DY = EY - PY;
                const float DZ = EZ - PZ;
                const float Distance = std::sqrt(DX * DX + DY * DY + DZ * DZ);
                if (Distance < 0.3f || Distance > BestDistance)
                {
                    continue;
                }
                if (ActorHealth(Entity) <= 0.0f)
                {
                    continue;
                }
                BestDistance = Distance;
                Best = Entity;
            }
            (void) Alive;
            if (Best)
            {
                Attack(reinterpret_cast<void*>(GameMode), Best);
            }
        }

        void TickOnce()
        {
            void* Player = LocalPlayer();
            if (!Player)
            {
                return;
            }
            TickKillAura(Player);
        }

        void* CarriedStack()
        {
            void* Player = LocalPlayer();
            if (!Player || gBase == 0)
            {
                return nullptr;
            }
            auto GetCarried = reinterpret_cast<void* (*)(void*)>(gBase + kPlayerCarriedItem);
            if (!mem::Readable(reinterpret_cast<uintptr_t>(GetCarried)))
            {
                return nullptr;
            }
            void* Stack = GetCarried(Player);
            if (!Stack || !mem::Readable(reinterpret_cast<uintptr_t>(Stack)))
            {
                return nullptr;
            }
            return Stack;
        }

        void CameraNoop(void*) {}

        bool InstallCameraHook()
        {
            if (gCameraHooked)
            {
                return true;
            }
            const uintptr_t Target = Addr();
            if (Target == 0)
            {
                GLOG("相机穿墙：拿不到 CameraAvoidanceSystem_tick 地址（base=0x%lx）",
                     static_cast<unsigned long>(gBase));
                return false;
            }
            if (!hook::Install(reinterpret_cast<void*>(Target),
                               reinterpret_cast<void*>(&CameraNoop), nullptr))
            {
                GLOG("相机穿墙：装 hook 失败 %s", hook::LastError());
                return false;
            }
            gCameraHooked = true;
            GLOG("相机穿墙：hook 已装 so+0x%lx",
                 static_cast<unsigned long>(kCameraAvoidanceTick));
            return true;
        }
    }

    void SetBase(uintptr_t Base, const char* Version)
    {
        if (gBase != 0)
        {
            return;
        }
        gBase = Base;
        const bool Ok = Base != 0 && Version && std::strncmp(Version, "3.10", 4) == 0;
        gVersionOk.store(Ok);
        if (!Ok)
        {
            GLOG("游戏侧开关关闭：版本 %s 没有地址表", Version ? Version : "未知");
        }
    }

    bool SetFeature(const char* Id, bool On)
    {
        if (!Id || !gVersionOk.load())
        {
            return false;
        }
        if (std::strcmp(Id, "cameraClip") == 0)
        {
            if (On && !InstallCameraHook())
            {
                return false;
            }
            gCameraClip.store(On);
            return true;
        }
        if (std::strcmp(Id, "killAura") == 0)
        {
            if (On && !InstallActorHook())
            {
                return false;
            }
            gKillAura.store(On);
            if (On)
            {
                StartTicker();
            }
            return true;
        }
        return false;
    }

    void StartTicker();

    bool Feature(const char* Id)
    {
        if (!Id)
        {
            return false;
        }
        if (std::strcmp(Id, "cameraClip") == 0)
        {
            return gCameraClip.load();
        }
        if (std::strcmp(Id, "killAura") == 0)
        {
            return gKillAura.load();
        }
        return false;
    }

    void StartTicker()
    {
        if (gTickerStarted.exchange(true))
        {
            return;
        }
        std::thread([] {
            for (;;)
            {
                usleep(50000);
                TickOnce();
            }
        }).detach();
    }

    bool Attach()
    {
        if (!gVersionOk.load())
        {
            return false;
        }
        return InstallLocalPlayerHook();
    }

    int HandDamage()
    {
        void* Stack = CarriedStack();
        if (!Stack)
        {
            return INT32_MIN;
        }
        int32_t Value = 0;
        if (!mem::ReadI32(reinterpret_cast<uintptr_t>(Stack) + kItemStackAux, Value))
        {
            return INT32_MIN;
        }
        return Value;
    }

    bool SetHandDamage(int Value)
    {
        void* Stack = CarriedStack();
        if (!Stack)
        {
            return false;
        }

        return mem::WriteI32(reinterpret_cast<uintptr_t>(Stack) + kItemStackAux, Value);
    }

    std::string HandInfo()
    {
        void* Player = LocalPlayer();
        if (!Player)
        {
            return std::string("没抓到玩家：先点「连接游戏」，并进世界");
        }
        void* Stack = CarriedStack();
        if (!Stack)
        {
            return std::string("CarriedStack 为空：手上没东西？");
        }
        const uintptr_t S = reinterpret_cast<uintptr_t>(Stack);
        unsigned char Raw[64] = { 0 };
        if (!mem::ReadRaw(S, Raw, sizeof(Raw)))
        {
            return std::string("ItemStack 读不到");
        }
        char Out[768];
        int Used = std::snprintf(Out, sizeof(Out), "stack=0x%lx\n", static_cast<unsigned long>(S));
        for (int i = 0; i < 64 && Used < static_cast<int>(sizeof(Out)) - 40; i += 8)
        {
            uint64_t Value = 0;
            std::memcpy(&Value, Raw + i, sizeof(Value));
            Used += std::snprintf(Out + Used, sizeof(Out) - Used, "+0x%02x=0x%llx\n", i,
                                  static_cast<unsigned long long>(Value));
        }
        return std::string(Out);
    }

    bool ApplyHandNbt(const char* Ops)    {
        if (!Ops || gBase == 0)
        {
            return false;
        }
        void* Stack = CarriedStack();
        if (!Stack)
        {
            return false;
        }
        const uintptr_t Slot = reinterpret_cast<uintptr_t>(Stack) + kItemStackUserData;
        uintptr_t Existing = 0;
        if (!mem::ReadPtr(Slot, Existing))
        {
            return false;
        }
        if (Existing != 0)
        {

            GLOG("NBT：这个物品已经有 tag（0x%lx），不覆盖", static_cast<unsigned long>(Existing));
            return false;
        }

        auto Ctor = reinterpret_cast<void (*)(void*)>(gBase + kCompoundCtor);
        auto PutString = reinterpret_cast<void (*)(void*, const std::string*, const std::string*)>(
            gBase + kPutString);
        auto PutByte = reinterpret_cast<void (*)(void*, const std::string*, uint8_t)>(gBase + kPutByte);
        auto PutShort = reinterpret_cast<void (*)(void*, const std::string*, int16_t)>(gBase + kPutShort);
        auto PutInt = reinterpret_cast<void (*)(void*, const std::string*, int32_t)>(gBase + kPutInt);
        auto PutInt64 = reinterpret_cast<void (*)(void*, const std::string*, int64_t)>(gBase + kPutInt64);
        auto PutFloat = reinterpret_cast<void (*)(void*, const std::string*, float)>(gBase + kPutFloat);
        auto PutDouble = reinterpret_cast<void (*)(void*, const std::string*, double)>(gBase + kPutDouble);
        auto PutCompound = reinterpret_cast<void (*)(void*, const std::string*, void*)>(gBase + kPutCompound);

        auto* Root = static_cast<void*>(std::calloc(1, kTagSize));
        if (!Root)
        {
            return false;
        }
        Ctor(Root);

        struct Pending
        {
            void* Tag;
            std::string Key;
        };
        std::vector<Pending> Path{ { Root, std::string() } };
        int Count = 0;

        const std::string Text(Ops);
        std::size_t LineStart = 0;
        while (LineStart < Text.size())
        {
            std::size_t LineEnd = Text.find('\n', LineStart);
            if (LineEnd == std::string::npos)
            {
                LineEnd = Text.size();
            }
            std::string Line = Text.substr(LineStart, LineEnd - LineStart);
            LineStart = LineEnd + 1;
            if (Line.empty() || Path.empty())
            {
                continue;
            }

            std::vector<std::string> Parts;
            std::size_t Cursor = 0;
            while (true)
            {
                std::size_t Tab = Line.find('\t', Cursor);
                if (Tab == std::string::npos)
                {
                    Parts.push_back(Line.substr(Cursor));
                    break;
                }
                Parts.push_back(Line.substr(Cursor, Tab - Cursor));
                Cursor = Tab + 1;
            }
            if (Parts.empty() || Parts[0].empty())
            {
                continue;
            }

            const char Kind = Parts[0][0];
            if (Kind == 'e')
            {
                if (Path.size() > 1)
                {
                    Pending Done = Path.back();
                    Path.pop_back();
                    const std::string Key = Done.Key;
                    PutCompound(Path.back().Tag, &Key, Done.Tag);
                    ++Count;
                }
                continue;
            }
            if (Parts.size() < 2)
            {
                continue;
            }
            const std::string& Key = Parts[1];
            void* Parent = Path.back().Tag;

            if (Kind == 'c')
            {
                auto* Child = static_cast<void*>(std::calloc(1, kTagSize));
                if (!Child)
                {
                    break;
                }
                Ctor(Child);
                Path.push_back({ Child, Key });
                continue;
            }

            const std::string Value = Parts.size() > 2 ? Parts[2] : std::string();
            switch (Kind)
            {
                case 's': PutString(Parent, &Key, &Value); ++Count; break;
                case 'b': PutByte(Parent, &Key, static_cast<uint8_t>(std::strtol(Value.c_str(), nullptr, 10))); ++Count; break;
                case 'h': PutShort(Parent, &Key, static_cast<int16_t>(std::strtol(Value.c_str(), nullptr, 10))); ++Count; break;
                case 'i': PutInt(Parent, &Key, static_cast<int32_t>(std::strtol(Value.c_str(), nullptr, 10))); ++Count; break;
                case 'l': PutInt64(Parent, &Key, std::strtoll(Value.c_str(), nullptr, 10)); ++Count; break;
                case 'f': PutFloat(Parent, &Key, std::strtof(Value.c_str(), nullptr)); ++Count; break;
                case 'd': PutDouble(Parent, &Key, std::strtod(Value.c_str(), nullptr)); ++Count; break;
                default: break;
            }
        }

        if (!mem::WritePtr(Slot, reinterpret_cast<uintptr_t>(Root)))
        {
            GLOG("NBT：写 ItemStack+16 失败");
            return false;
        }
        GLOG("NBT：已挂到手上物品，写入 %d 个键", Count);
        return true;
    }
}
