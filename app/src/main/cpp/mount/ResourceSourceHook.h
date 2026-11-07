#pragma once
#include "ResourceSourcePaths.h"
#include <cstdint>
#include <string>

namespace resource_mount {

bool Configure(const std::string& suppliedDataDir, const std::string& canonicalDataDir,
               const std::string& aliasSuffix, const std::string& storagePath);
bool Install(uintptr_t minecraftBase);
bool IsInstalled();
bool PendingInstallation();
Route MapPath(const char* input, std::array<char, kMaxPath>& output);
std::string Status();
}
