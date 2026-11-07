#pragma once

#include <cstddef>
#include <cstdint>

namespace texinject::hook
{

    bool Install(void* Target, void* Replace, void** Trampoline);

    const char* LastError();

    int Count();
}
