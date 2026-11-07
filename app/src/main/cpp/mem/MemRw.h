#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace texinject::mem
{

    bool ReadRaw(uintptr_t Address, void* Out, std::size_t Length);

    bool WriteRaw(uintptr_t Address, const void* Data, std::size_t Length);

    uintptr_t ModuleBase(const char* Name);

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

    bool ReadCString(uintptr_t Address, std::string& Out, std::size_t Max = 256);

    bool Readable(uintptr_t Address);
}
