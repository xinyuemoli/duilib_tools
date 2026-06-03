#pragma once
#include <windows.h>

HMODULE GetDuiLibModule();
PVOID FindDuiLibExport(const char* funcName);