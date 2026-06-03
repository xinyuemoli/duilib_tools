#include "ModuleFinder.h"

static HMODULE g_hDuiLib = nullptr;

HMODULE GetDuiLibModule() {
    if (!g_hDuiLib) {
        g_hDuiLib = GetModuleHandleA("duilib.dll");
        if (!g_hDuiLib) {
            OutputDebugStringA("[HookDll] duilib.dll not found\n");
            return nullptr;
        }
    }
    return g_hDuiLib;
}

PVOID FindDuiLibExport(const char* funcName) {
    if (!g_hDuiLib) {
        g_hDuiLib = GetDuiLibModule();
    }
    if (!g_hDuiLib) {
        return nullptr;
    }
    return GetProcAddress(g_hDuiLib, funcName);
}