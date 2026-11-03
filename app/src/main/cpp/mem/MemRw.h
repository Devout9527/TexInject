/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 进程内内存读写。
 *
 * 用 process_vm_readv / process_vm_writev 而不是 /proc/self/mem：
 *   - 不需要打开文件描述符，不占 fd，也不受 SELinux 对 /proc 的限制
 *   - 一次系统调用完成，失败只返回 -1，不会像读写 /proc/self/mem 那样
 *     因为地址无效而把整个进程 SIGBUS 掉
 * 所有接口在地址无效时返回 false / 0，调用方不用自己 try。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace texinject::mem
{
    /** 读一块内存。地址无效或越界返回 false。 */
    bool ReadRaw(uintptr_t Address, void* Out, std::size_t Length);

    /** 写一块内存。目标不可写返回 false。 */
    bool WriteRaw(uintptr_t Address, const void* Data, std::size_t Length);

    /**
     * 取已加载 so 的基址（/proc/self/maps 里 offset=0 的那一段）。
     * Name 只给文件名，如 "libminecraftpe.so"。找不到返回 0。
     */
    uintptr_t ModuleBase(const char* Name);

    // ---- 便捷读写：失败时返回 0 / false ----
    bool ReadU8(uintptr_t Address, uint8_t& Out);
    bool ReadI32(uintptr_t Address, int32_t& Out);
    bool ReadI64(uintptr_t Address, int64_t& Out);
    bool ReadF32(uintptr_t Address, float& Out);
    bool ReadF64(uintptr_t Address, double& Out);
    bool ReadPtr(uintptr_t Address, uintptr_t& Out);

    bool WriteU8(uintptr_t Address, uint8_t Value);
    bool WriteI32(uintptr_t Address, int32_t Value);
    bool WriteI64(uintptr_t Address, int64_t Value);
    bool WriteF32(uintptr_t Address, float Value);
    bool WriteF64(uintptr_t Address, double Value);
    bool WritePtr(uintptr_t Address, uintptr_t Value);

    /** 读 C 字符串（最多 Max 字节），用于看游戏里的名字/路径。 */
    bool ReadCString(uintptr_t Address, std::string& Out, std::size_t Max = 256);

    /** 这个地址能不能读（试着读 4 字节）。装 hook 前用它确认地址表没对错。 */
    bool Readable(uintptr_t Address);
}
