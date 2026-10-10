/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "Signature.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/mman.h>

namespace sig {

// ---------------- 签名解析 ----------------

static int hex1(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Pattern compile(const std::string& pattern) {
    Pattern p;
    std::istringstream iss(pattern);
    std::string tok;
    while (iss >> tok) {
        if (tok == "?" || tok == "??") {
            p.value.push_back(0);
            p.mask.push_back(0);
            continue;
        }
        if (tok.size() != 2) return Pattern{};      // 非法
        uint8_t v = 0, m = 0;
        int hi = hex1(tok[0]), lo = hex1(tok[1]);
        if (tok[0] == '?') { /* hi 不匹配 */ } else if (hi >= 0) { v |= hi << 4; m |= 0xF0; } else return Pattern{};
        if (tok[1] == '?') { /* lo 不匹配 */ } else if (lo >= 0) { v |= lo;      m |= 0x0F; } else return Pattern{};
        p.value.push_back(v);
        p.mask.push_back(m);
    }
    p.ok = !p.value.empty();
    return p;
}

// ---------------- 模块区间 ----------------

Module::Module(const std::string& name) {
    std::ifstream f("/proc/self/maps");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        // 7000d00000-7000d1b000 r-xp ... /data/app/.../libminecraftpe.so
        if (line.find(name) == std::string::npos) continue;
        if (line.find(" r-xp ") == std::string::npos) continue;
        unsigned long a = 0, b = 0;
        if (std::sscanf(line.c_str(), "%lx-%lx", &a, &b) != 2) continue;
        Range r{a, static_cast<size_t>(b - a)};
        // 合并相邻可执行页
        if (!ranges_.empty() && ranges_.back().start + ranges_.back().size == r.start)
            ranges_.back().size += r.size;
        else
            ranges_.push_back(r);
    }
}

// ---------------- 扫描 ----------------

bool matchesAt(uintptr_t addr, const Pattern& pat) {
    if (!pat.ok) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(addr);
    for (size_t i = 0; i < pat.value.size(); ++i)
        if ((d[i] & pat.mask[i]) != pat.value[i]) return false;
    return true;
}

uintptr_t scan(const Module& mod, const Pattern& pat) {
    if (!pat.ok) return 0;
    const size_t n = pat.value.size();
    for (const Range& r : mod.ranges()) {
        const uint8_t* base = reinterpret_cast<const uint8_t*>(r.start);
        if (r.size < n) continue;
        for (size_t i = 0; i + n <= r.size; ++i) {
            const uint8_t* p = base + i;
            // 先用首字节快速筛
            if ((p[0] & pat.mask[0]) != pat.value[0]) continue;
            size_t j = 1;
            for (; j < n; ++j)
                if ((p[j] & pat.mask[j]) != pat.value[j]) break;
            if (j == n) return r.start + i;
        }
    }
    return 0;
}

uintptr_t find(const Module& mod, const std::string& pattern) {
    return scan(mod, compile(pattern));
}

} // namespace sig
