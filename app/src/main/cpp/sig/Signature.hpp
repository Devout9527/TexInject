/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 字节签名扫描器（移植自 LiteLDev/preloader-android 的 pl::memory::Signature，
 * 重写为不依赖任何 hook 框架的独立实现）。
 *
 * 签名格式（与 LeviLaunchroid 一致，支持半字节通配）：
 *     "F9 4C C0 00 D6 5F 03 C0"        精确匹配
 *     "FD 7B BC A9 ? ? ? 94"           整字节通配
 *     "08 E0 41 39 08 ?? ?? 34"        同上
 *     "0? 1? 2? 3?"                    半字节通配
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sig {

struct Range { uintptr_t start; size_t size; };

/** 已加载模块的可扫描区间（构造时自动从 /proc/self/maps 解析）。 */
class Module {
public:
    explicit Module(const std::string& name);

    bool valid() const { return !ranges_.empty(); }
    const std::vector<Range>& ranges() const { return ranges_; }
    uintptr_t base() const { return ranges_.empty() ? 0 : ranges_.front().start; }

private:
    std::vector<Range> ranges_;
};

/** 编译后的单条模式。 */
struct Pattern {
    std::vector<uint8_t> value;   // 期望值
    std::vector<uint8_t> mask;    // 1 = 该位必须匹配
    bool ok = false;
};

Pattern compile(const std::string& pattern);

/** 在某模块内扫描，返回第一个命中的绝对地址；找不到返回 0。
 *  只扫可执行段。 */
uintptr_t scan(const Module& mod, const Pattern& pat);

/** 便捷：编译 + 扫描。 */
uintptr_t find(const Module& mod, const std::string& pattern);

/** 在 [addr, addr+len) 内校验模式是否成立（用于验证候选）。 */
bool matchesAt(uintptr_t addr, const Pattern& pat);

} // namespace sig
