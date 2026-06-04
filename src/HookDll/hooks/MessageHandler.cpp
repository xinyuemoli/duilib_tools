#include "MessageHandler.h"
#include "../duilib/ModuleFinder.h"
#include "../common/ThreadEnum.h"
#include "../accessible/Accessible.h"
#include "detours/detours.h"
#include <vector>
#include <string>
#include <format>

class CPaintManagerUI_Hook {
public:
    BOOL DetouredMessageHandler(UINT msg, UINT wParam, INT lParam, INT* pResult);
};

typedef BOOL(CPaintManagerUI_Hook::* FN_MessageHandler)(UINT msg, UINT wParam, INT lParam, INT* pResult);

static FN_MessageHandler g_originPainterMessageHandle = nullptr;
static PVOID g_pRawOriginMessageHandler = nullptr;
static BOOL g_bHookInstalled = FALSE;

BOOL CPaintManagerUI_Hook::DetouredMessageHandler(UINT msg, UINT wParam, INT lParam, INT* pResult) {
    // Handle WM_GETOBJECT only when Accessible is properly initialized
    if (msg == WM_GETOBJECT && lParam == OBJID_CLIENT && g_bAccessibleInitialized) {
        OutputDebugStringA("[HookDll] WM_GETOBJECT received\n");
        if (g_pfnGetRoot && g_pfnGetPaintWindow) {
            HWND hwnd = g_pfnGetPaintWindow(this);
            OutputDebugStringA("[HookDll] got paint window\n");
            void* pRootControl = g_pfnGetRoot(this);
            OutputDebugStringA("[HookDll] got root control\n");
            if (pRootControl) {
                IDispatch* pRootAccessible = GetOrCreateAccessibleWrapper(hwnd, this, pRootControl, nullptr);
                if (pRootAccessible) {
                    LRESULT lres = LresultFromObject(IID_IAccessible, wParam, pRootAccessible);
                    pRootAccessible->Release();
                    *pResult = lres;
                    OutputDebugStringA("[HookDll] WM_GETOBJECT handled OK\n");
                    return TRUE;
                }
            }
        }
    }

    // Default message handling
    BOOL bResult = (this->*g_originPainterMessageHandle)(msg, wParam, lParam, pResult);
    return bResult;
}

static void FindImportFunctions() {
    PVOID pfnRaw = FindDuiLibExport("?MessageHandler@CPaintManagerUI@DuiLib@@QAE_NIIJAAJ@Z");
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

    // Initialize Accessible module after DuiLib symbols are bound
    InitAccessibleModule();
}

BOOL InstallMessageHandlerHook() {
    OutputDebugStringA("[HookDll] enter InstallMessageHandlerHook!\n");
    if (!g_pRawOriginMessageHandler) {
        FindImportFunctions();
    }
    if (!g_pRawOriginMessageHandler) {
        OutputDebugStringA("[HookDll] g_pRawOriginMessageHandler is null!\n");
        return FALSE;
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

    OutputDebugStringA("[HookDll] leave InstallMessageHandlerHook!\n");
    return g_bHookInstalled;
}

BOOL UninstallMessageHandlerHook() {
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