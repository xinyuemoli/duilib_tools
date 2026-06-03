#include <windows.h>
#include "hooks/MessageHandler.h"

DWORD WINAPI InitHookThread(LPVOID) {
    OutputDebugStringA("[HookDll] InitHookThread\n");

    if (!InstallMessageHandlerHook()) {
        OutputDebugStringA("[HookDll] Load failed\n");
        return 0;
    }

    OutputDebugStringA("[HookDll] Load successed\n");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        HANDLE hThread = CreateThread(NULL, 0, InitHookThread, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        if (lpReserved == NULL) {
            UninstallMessageHandlerHook();
        }
    }
    return TRUE;
}