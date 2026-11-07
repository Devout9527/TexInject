#ifndef MINECRAFT_MAIN_H
#define MINECRAFT_MAIN_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <android/log.h>

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

#endif
