/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 抓 LocalPlayer 指针。
 *
 * 这是所有「改游戏状态」类功能的地基：没有这个指针，自动工具 / 耐久 /
 * 背包整理 / 刷物品 全都无从下手。
 *
 * 手段是 inline hook `LocalPlayer::normalTick(this)` —— 它的第一个参数就是
 * LocalPlayer*。这函数签名我们验过：
 *   - 3.10.5  0x759b6c8（vtable 反推，10/10 成员对齐）
 *   - 3.9.15  0x074c79fc
 * 头部指令是 str d8,[sp,#-0x70]! / stp x29,x30,... 不含 PC 相对寻址，可以搬。
 */
#pragma once

#include <cstdint>

namespace texinject::game
{
    /** 由 NativeCore.setGameVersion 带进来，用来选地址。 */
    void SetGameVersion(const char* Version);

    /** 等 so 加载后调用；失败返回 false，原因见日志。 */
    bool InstallLocalPlayerHook();

    /** 当前抓到的 LocalPlayer 指针，还没 tick 过就是 nullptr。 */
    void* LocalPlayer();

    /** 是否已经抓到过（UI 可以据此显示"已就绪"）。 */
    bool HasLocalPlayer();
}
