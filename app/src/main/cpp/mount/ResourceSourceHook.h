#pragma once
#include "ResourceSourcePaths.h"
#include <cstdint>
#include <string>

namespace resource_mount {
// Control/loader workers only. Configuration is immutable for this process;
// changing an active mount requires a restart, not a half-published stack.
bool Configure(const std::string& suppliedDataDir, const std::string& canonicalDataDir,
               const std::string& aliasSuffix, const std::string& storagePath);
bool Install(uintptr_t minecraftBase);
bool IsInstalled();
bool PendingInstallation();
Route MapPath(const char* input, std::array<char, kMaxPath>& output);
std::string Status();
} // namespace resource_mount
