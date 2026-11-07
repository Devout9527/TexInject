#pragma once

#include <cstdint>

namespace texinject::game
{

    void SetGameVersion(const char* Version);

    bool InstallLocalPlayerHook();

    void* LocalPlayer();

    bool HasLocalPlayer();

    bool InWorld(int64_t WindowMs = 1500);
}
