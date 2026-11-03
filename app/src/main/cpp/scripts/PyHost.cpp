/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Python 加载器的宿主侧。
 *
 * 逻辑照搬 bedrock-python-xposed（MIT）：采 so 映像 → 特征扫描点名 → ABI 见证校验 →
 * 在 Player tick 上执行脚本。这里只把它的 DobbyHook 换成我们自己的 hook::Install，
 * 其余流程、校验项、顺序都不动。
 *
 * 为什么必须这样：游戏把 opcode 表打乱了，标准 CPython 编出来的字节码直接喂进去
 * 会执行到别的指令上。所以编译在私有解释器里做（python/），把 opcode 按
 * Python3105Bytecode.h 换成游戏编号，再由游戏自己的 marshal 读进来。
 * PyRun_StringFlags 那条路是死的（编译完无条件被拦），不走。
 */
#include "common/Signatures.h"
#include "common/PyFunctionsAddress.h"
#include "scripts/ScriptRuntime.h"
#include "scripts/PythonScriptAbi3105.h"
#include "hook/InlineHook.h"

#include <android/log.h>
#include <jni.h>
#include <link.h>
#include <unistd.h>
#include <fcntl.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

#define PYHOST_TAG "TexInjectPy"

namespace script_host
{
namespace
{
std::mutex InitMutex;
std::string Status = "等待游戏原生库";
void* Original = nullptr;
bool Installed = false;
std::atomic<bool> Running{ false };

/** Player tick 的替身：先跑原函数，再把握到的玩家/维度交给脚本队列。 */
void Tick(void* Actor)
{
    auto Stock = reinterpret_cast<void (*)(void*)>(__atomic_load_n(&Original, __ATOMIC_ACQUIRE));
    if (!Stock) return;
    Stock(Actor);
    if (!Running.load(std::memory_order_acquire) || !Actor) return;

    // xposed 的做法：先读 +0x1998 那个字段，再用游戏的 getter 确认是同一个人，
    // 两者对不上就说明这个 tick 不是当前本地玩家，直接放过。
    uintptr_t Client = 0;
    if (!Read(uintptr_t(Actor) + PyFunctionsAddress::Actor_clientFieldOffset, &Client, sizeof(Client)) || !Client)
        return;
    const uintptr_t GameBase = script_host::Base();
    if (reinterpret_cast<void* (*)(void*)>(GameBase + PyFunctionsAddress::Actor_client)(Actor) !=
        reinterpret_cast<void*>(Client))
        return;
    uintptr_t Header = 0;
    if (!Read(Client, &Header, sizeof(Header))) return;
    auto* Dimension = reinterpret_cast<void* (*)(void*)>(GameBase + PyFunctionsAddress::Actor_dimension)(Actor);
    if (!Dimension) return;

    static thread_local uintptr_t LastActor = 0, LastDimension = 0;
    static thread_local auto Last = std::chrono::steady_clock::now();
    const auto Now = std::chrono::steady_clock::now();
    if (LastActor && (LastActor != uintptr_t(Actor) || LastDimension != uintptr_t(Dimension) ||
                      Now - Last > std::chrono::seconds(2)))
        local_python::OnSessionBoundary();
    LastActor = uintptr_t(Actor);
    LastDimension = uintptr_t(Dimension);
    Last = Now;
    local_python::OnLocalPlayerTick(Actor, uintptr_t(Dimension));
}

int Gather(dl_phdr_info* Info, size_t, void* Data)
{
    if (!Info->dlpi_name || !std::strstr(Info->dlpi_name, "libminecraftpe.so")) return 0;
    auto& Img = *static_cast<Image*>(Data);
    Img.base = Info->dlpi_addr;
    for (size_t i = 0; i < Info->dlpi_phnum; ++i)
    {
        const auto& P = Info->dlpi_phdr[i];
        if (P.p_vaddr > UINTPTR_MAX - Img.base || P.p_memsz > UINTPTR_MAX - Img.base - P.p_vaddr)
            continue;
        const uintptr_t Start = Img.base + P.p_vaddr;
        if (P.p_type == PT_LOAD && P.p_flags & PF_R)
        {
            arm64_signature::CodeRange R{ reinterpret_cast<const uint8_t*>(Start), size_t(P.p_memsz), Start };
            if (P.p_flags & PF_X)
            {
                R.size = P.p_filesz;
                Img.code.push_back(R);
            }
            else
            {
                Img.data.push_back(R);
            }
            if (P.p_flags & PF_W) Img.writable.push_back(R);
        }
        if (P.p_type == PT_NOTE)
        {
            size_t Cursor = 0;
            while (Cursor + 12 <= P.p_filesz)
            {
                uint32_t Note[3];
                if (!Read(Start + Cursor, Note, 12)) break;
                Cursor += 12;
                const size_t NameLen = (size_t(Note[0]) + 3) & ~size_t(3);
                const size_t DescLen = (size_t(Note[1]) + 3) & ~size_t(3);
                if (NameLen > P.p_filesz - Cursor || DescLen > P.p_filesz - Cursor - NameLen) break;
                char Name[4]{};
                if (Note[0] == 4 && Note[2] == 3 && Read(Start + Cursor, Name, 4) &&
                    !std::memcmp(Name, "GNU", 4) && Note[1] <= 64)
                {
                    Img.buildId.resize(Note[1]);
                    if (!Read(Start + Cursor + NameLen, Img.buildId.data(), Img.buildId.size()))
                        Img.buildId.clear();
                }
                Cursor += NameLen + DescLen;
            }
        }
    }
    return 1;
}
}

/**
 * 读游戏内存。
 *
 * 走 pipe 而不是 process_vm_readv：目标页可能只可读不可是别的进程能碰的，
 * 而 pipe 的 write() 在内核里直接复制，对 PROT_READ 页最稳，且不需要 fd 常驻。
 */
bool Read(uintptr_t Address, void* Output, size_t Size) noexcept
{
    if (!Address || !Output || !Size || Size > 4096 || Size > UINTPTR_MAX - Address) return false;
    struct Channel
    {
        int Fd[2]{ -1, -1 };
        ~Channel() { Reset(); }
        void Reset()
        {
            for (int& F : Fd)
            {
                if (F >= 0) close(F);
                F = -1;
            }
        }
    };
    static thread_local Channel Pipe;
    if (Pipe.Fd[0] < 0 && pipe2(Pipe.Fd, O_CLOEXEC | O_NONBLOCK)) return false;

    ssize_t Written;
    do
    {
        Written = write(Pipe.Fd[1], reinterpret_cast<const void*>(Address), Size);
    } while (Written < 0 && errno == EINTR);
    if (Written != ssize_t(Size))
    {
        Pipe.Reset();
        return false;
    }
    ssize_t Received;
    do
    {
        Received = read(Pipe.Fd[0], Output, Size);
    } while (Received < 0 && errno == EINTR);
    if (Received != ssize_t(Size))
    {
        Pipe.Reset();
        return false;
    }
    return true;
}

bool EnsureTickObserver()
{
    std::lock_guard<std::mutex> Lock(InitMutex);
    if (Installed) return Running.load();

    Image Img;
    dl_iterate_phdr(Gather, &Img);
    if (!Img.base)
    {
        Status = "等待 libminecraftpe.so 加载";
        return false;
    }
    if (!Resolve(Img, &Status))
        return false;

    local_python::abi::Targets Api;
    if (!local_python::abi::Verify(
            Img.base, &Api,
            [&](uintptr_t Offset, uintptr_t* Address) {
                return CodeOffset(Img.base, Offset, 4, Address);
            },
            [&](uintptr_t Offset, size_t, uintptr_t* Address) {
                *Address = Img.base + Offset;
                return PyFunctionsAddress::IsResolvedAddress(Img.base, *Address);
            },
            Read))
    {
        Status = "Python 对象／字节码 ABI 校验失败";
        return false;
    }

    const uintptr_t Target = Img.base + PyFunctionsAddress::Player_normalTick;
    const bool Ok = texinject::hook::Install(reinterpret_cast<void*>(Target),
                                             reinterpret_cast<void*>(&Tick),
                                             reinterpret_cast<void**>(&Original));
    Installed = Ok && Original != nullptr;
    Running.store(Installed, std::memory_order_release);
    Status = Installed ? "已就绪：3.10.5 / Python 2.7" : "Player tick Hook 安装失败";
    __android_log_print(ANDROID_LOG_INFO, PYHOST_TAG, "%s（%s）", Status.c_str(),
                        texinject::hook::LastError());
    return Running;
}

std::string InitializationStatus()
{
    std::lock_guard<std::mutex> Lock(InitMutex);
    return Status;
}

bool Operational() noexcept
{
    return Running.load(std::memory_order_acquire);
}
}
