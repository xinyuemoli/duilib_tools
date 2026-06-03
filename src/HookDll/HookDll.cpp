#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <format>
#include "detours/detours.h"

// Required declarations
static HMODULE g_hDuiLib = NULL;

std::vector<HANDLE> GetAllOtherThreadsInProcess() {
    std::vector<HANDLE> otherThreads;

    DWORD currentProcessId = GetCurrentProcessId();
    DWORD currentThreadId = GetCurrentThreadId();

    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[HookDll] CreateToolhelp32Snapshot failed!\n");
        return otherThreads;
    }

    THREADENTRY32 te32;
    te32.dwSize = sizeof(te32);

    if (Thread32First(hThreadSnap, &te32)) {
        do {
            if (te32.th32OwnerProcessID == currentProcessId) {
                if (te32.th32ThreadID != currentThreadId) {
                    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
                    if (hThread != NULL) {
                        otherThreads.push_back(hThread);
                    }
                }
            }
        } while (Thread32Next(hThreadSnap, &te32));
    }

    CloseHandle(hThreadSnap);
    return otherThreads;
}

class CPaintManagerUI_Hook {
public:
    BOOL DetouredMessageHandler(UINT msg, UINT wParam, INT lParam, INT* pResult);
};

typedef BOOL(CPaintManagerUI_Hook::* FN_MessageHandler)(UINT msg, UINT wParam, INT lParam, INT* pResult);

static FN_MessageHandler g_originPainterMessageHandle = nullptr;
static PVOID g_pRawOriginMessageHandler = nullptr;
BOOL g_bHookInstalled = FALSE;

BOOL CPaintManagerUI_Hook::DetouredMessageHandler(UINT msg, UINT wParam, INT lParam, INT* pResult) {
    OutputDebugStringA("[HookDll] call g_originPainterMessageHandle\n");

    BOOL bResult = (this->*g_originPainterMessageHandle)(msg, wParam, lParam, pResult);

    OutputDebugStringA("[HookDll] leave DetouredMessageHandler \n");
    return bResult;
}

void FindImportFunctions()
{
    PVOID pfnRaw = (PVOID)GetProcAddress(g_hDuiLib, "?MessageHandler@CPaintManagerUI@DuiLib@@QAE_NIIJAAJ@Z");
    if (!pfnRaw) {
        OutputDebugStringA("[HookDll] MessageHandler not found\n");
        return;
    }

    g_pRawOriginMessageHandler = pfnRaw;

    union {
        PVOID raw;
        FN_MessageHandler member;
    } cast;
    cast.raw = pfnRaw;
    g_originPainterMessageHandle = cast.member;
}

BOOL InstallMessageHandlerHook2()
{
    OutputDebugStringA("[HookDll] enter InstallMessageHandlerHook2!\n");
    
    if (!g_pRawOriginMessageHandler) {
        OutputDebugStringA("[HookDll] g_pRawOriginMessageHandler is null!\n");
        return FALSE;
    }

    DetourTransactionBegin();
    
    std::vector<HANDLE> threads = GetAllOtherThreadsInProcess();
    for(HANDLE thread : threads) {
        DetourUpdateThread(thread);
    }

    union {
        FN_MessageHandler member;
        PVOID raw;
    } detourCast;
    detourCast.member = &CPaintManagerUI_Hook::DetouredMessageHandler;

    DetourAttach(&g_pRawOriginMessageHandler, detourCast.raw);

    LONG result = DetourTransactionCommit();
    
    for (HANDLE thread : threads) {
        CloseHandle(thread);
    }
    threads.clear();

    if (result == NO_ERROR) {
        union {
            PVOID raw;
            FN_MessageHandler member;
        } reCast;
        reCast.raw = g_pRawOriginMessageHandler;
        g_originPainterMessageHandle = reCast.member;

        g_bHookInstalled = TRUE;
        OutputDebugStringA("[HookDll] MessageHandler HOOKED!\n");
    } else {
        std::string errStr = std::format("[HookDll] DetourTransactionCommit failed with error: {}\n", result);
        OutputDebugStringA(errStr.c_str());
    }

    OutputDebugStringA("[HookDll] leave InstallMessageHandlerHook2!\n");
    return g_bHookInstalled;
}

BOOL UnInstallMessageHandlerHook2() {
    if (!g_bHookInstalled || !g_pRawOriginMessageHandler) {
        return TRUE;
    }

    DetourTransactionBegin();
    
    std::vector<HANDLE> threads = GetAllOtherThreadsInProcess();
    for (HANDLE thread : threads) {
        DetourUpdateThread(thread);
    }
        
    union {
        FN_MessageHandler member;
        PVOID raw;
    } detourCast;
    detourCast.member = &CPaintManagerUI_Hook::DetouredMessageHandler;

    DetourDetach(&g_pRawOriginMessageHandler, detourCast.raw);

    LONG result = DetourTransactionCommit();

    for (HANDLE thread : threads) {
        CloseHandle(thread);
    }
    threads.clear();

    if (result == NO_ERROR) {
        g_bHookInstalled = FALSE;
        OutputDebugStringA("[HookDll] MessageHandler UnHOOKED!\n");
    } else {
        OutputDebugStringA("[HookDll] UnHOOKED failed!\n");
    }

    return !g_bHookInstalled;
}

BOOL GetDuilibModule()
{
    if (!g_hDuiLib)
    {
        g_hDuiLib = GetModuleHandleA("duilib.dll");
        if (!g_hDuiLib) {
            OutputDebugStringA("[HookDll] duilib.dll not found\n");
            return FALSE;
        }
    }
    return g_hDuiLib != NULL;
}

DWORD WINAPI InitHookThread(LPVOID) {
    OutputDebugStringA("[HookDll] InitHookThread\n");
    if (!GetDuilibModule()) {
        OutputDebugStringA("[HookDll] leave1 InitHookThread\n");
        return 0;
    }

    FindImportFunctions();
    BOOL suc = InstallMessageHandlerHook2();
    if (suc) {
        OutputDebugStringA("[HookDll] Load successed\n");
    } else {
        OutputDebugStringA("[HookDll] Load failed\n");
    }

    OutputDebugStringA("[HookDll] leave2 InitHookThread\n");
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
            UnInstallMessageHandlerHook2();
        }
    }
    return TRUE;
}
