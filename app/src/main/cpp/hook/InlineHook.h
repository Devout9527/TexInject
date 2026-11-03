/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * ARM64 inline hook（最小可用版）。
 *
 * 为什么不用 Orange 的 ArmMemHook：它那个 Hook() 只 mprotect 了一下、
 * 往表里记一条就 return true，一个字节都没写到目标函数上 —— 全是空操作。
 *
 * 这里的做法是标准的头部 16 字节覆盖：
 *
 *   原函数:                         换成:
 *   +0  sub  sp, sp, #0x40          +0   ldr  x16, #8      (0x58000050)
 *   +4  stp  x29, x30, [sp, #0x30]  +4   br   x16          (0xD61F0200)
 *   +8  ...                         +8   .quad detour      ← 跳去我们的函数
 *   +12 ...                         +16  原来的 4 条指令（在 trampoline 里）
 *
 * trampoline 是一段 mmap 出来的可执行内存，里面放被覆盖的 4 条指令，
 * 然后跳回 原函数+16 继续执行。
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace texinject::hook
{
    /**
     * 在 Target 头部装一个跳转，跳去 Replace。
     *
     * @param Trampoline 输出参数：调用它等于调用原函数（已跳过被覆盖的指令）。
     *                   传 nullptr 表示不需要调用原函数。
     * @return 是否成功。失败原因会写进 LastError()。
     */
    bool Install(void* Target, void* Replace, void** Trampoline);

    /** 上一次失败的原因（中文，直接可以写日志）。 */
    const char* LastError();

    /** 已安装的 hook 数量。 */
    int Count();
}
