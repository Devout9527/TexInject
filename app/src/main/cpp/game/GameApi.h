#pragma once

#include <cstdint>
#include <string>

namespace texinject::game
{

    void StartTicker();

    void SetBase(uintptr_t Base, const char* Version);

    bool SetFeature(const char* Id, bool On);

    bool Feature(const char* Id);

    bool Attach();

    int HandDamage();

    bool SetHandDamage(int Value);

    std::string HandInfo();

    bool ApplyHandNbt(const char* Ops);
}
