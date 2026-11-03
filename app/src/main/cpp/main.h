/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 * TexInject is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * TexInject is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with TexInject. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef MINECRAFT_MAIN_H
#define MINECRAFT_MAIN_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <android/log.h>

// LOGE 宏由各翻译单元自行定义，避免重定义冲突
// 如需日志，请在 .cpp 文件中定义:
//   #define LOG_TAG "YourTag"
//   #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

class Main {
public:
    static std::atomic<uintptr_t> baseAddress;

    static uintptr_t getBaseAddress() noexcept {
        return baseAddress.load(std::memory_order_acquire);
    }

    static void setBaseAddress(uintptr_t value) noexcept {
        baseAddress.store(value, std::memory_order_release);
    }
};


#endif // MINECRAFT_MAIN_H
