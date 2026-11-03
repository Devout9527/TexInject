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
#ifndef TEXINJECT_PLT_HOOK_H
#define TEXINJECT_PLT_HOOK_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * GOT / PLT hook。
 *
 * 为什么要用 GOT 而不是 inline hook：
 *
 *   1. 不改游戏代码段。inline hook 要往 .text 里写跳转，一旦网易加了 CRC
 *      校验（或者反作弊扫代码段），立刻暴露；GOT 在数据段，改动小得多。
 *   2. 不用做 ARM64 指令重定位。inline hook 必须处理 B/BL、ADR/ADRP、
 *      LDR literal、CBZ 这些 pc 相关的指令，写错一个就随机崩。
 *   3. 我们的目标（libc 的 openat/fopen/stat、libEGL 的 eglSwapBuffers）
 *      全部是【导入函数】，天然走 GOT。
 *
 * 代价：只能 hook 通过 PLT/GOT 调用的外部函数，hook 不到游戏内部函数。
 *       要 hook 内部函数，得先靠这里拿到的「调用者 RVA」反推地址，再另做处理。
 *
 * 另外一个副作用很有用：hook 到之后能拿到 __builtin_return_address(0)，
 * 减掉模块基址就是调用点在 libminecraftpe.so 里的 RVA —— 这正是定位游戏
 * 内部函数最直接的办法。
 */
namespace Plt {

/** 一个导入符号的 GOT 槽。orig 是 replace 之前的真实函数地址。 */
struct Entry {
    std::string name;
    void** slot;
    void* orig;
};

/**
 * 解析目标 so 的重定位表，把所有导入符号的 GOT 槽缓存下来。
 *
 * 同时读 .rela.plt（DT_JMPREL / R_AARCH64_JUMP_SLOT）和
 * .rela.dyn（DT_RELA / R_AARCH64_GLOB_DAT）：Android 10 之后默认
 * full RELRO + immediate binding，大部分函数调用落在 .rela.dyn 里，
 * 只看 .rela.plt 会漏掉一大半。
 *
 * 可重复调用：已初始化成功则直接返回 true。
 */
bool init();

/** 模块基址是否已就绪。 */
bool ready();

/** 缓存的导入符号数量（自检用，正常应该有几百上千个）。 */
int importCount();

/** 找符号当前的 GOT 槽地址；没有这个符号返回 nullptr。 */
void** find(const char* symbol);

/**
 * 把符号的 GOT 项换成 replacement。
 *
 * @return 原函数地址（给 trampoline 用）；失败返回 nullptr。
 *         注意：如果原来还是 lazy binding 状态，GOT 里放的是 PLT stub，
 *         这里会用 dlsym 解析出真实地址后再写。
 */
void* replace(const char* symbol, void* replacement);

/** 把符号的 GOT 项恢复到 replace 之前的原函数。 */
bool restore(const char* symbol);

/** 最近一次失败原因。 */
const char* lastError();

/** 把已知的导入符号名 dump 成一行一个，用于离线分析。 */
std::vector<std::string> allNames();

}  // namespace Plt

#endif  // TEXINJECT_PLT_HOOK_H
