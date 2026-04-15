#pragma once

#include <windows.h>

#include <string>

namespace shogun2 {

void SetSelfModule(HMODULE module) noexcept;
HMODULE GetSelfModule() noexcept;
std::wstring GetModuleDirectory();
void Log(const char* format, ...);

}  // namespace shogun2
