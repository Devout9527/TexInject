#pragma once
#include <cstdint>
#include <string>
namespace local_python {
bool SubmitPath(const std::string& utf8Path);
std::string StatusJson();
void OnLocalPlayerTick(void* actor,uintptr_t dimension) noexcept;
void OnSessionBoundary() noexcept;
std::string QuoteStatus(const std::string&);
bool Busy();
bool LastSucceeded();
void Shutdown() noexcept;
}
