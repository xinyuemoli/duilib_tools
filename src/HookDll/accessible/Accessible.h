#pragma once
#include <windows.h>
#include <oleacc.h>
#include <map>

// ==========================================
// DuiLib Function Pointer Types
// Only for non-virtual functions resolved via GetProcAddress
// ==========================================
typedef void* (__thiscall* FN_GetRoot)(void* pPM);
typedef HWND  (__thiscall* FN_GetPaintWindow)(void* pPM);

// Global DuiLib function pointers (non-virtual only)
extern FN_GetRoot        g_pfnGetRoot;
extern FN_GetPaintWindow g_pfnGetPaintWindow;
extern BOOL g_bAccessibleInitialized;

// CDuiString::GetData (non-virtual)
typedef const wchar_t* (__thiscall* FN_DuiStringGetData)(void* pDuiString);
extern FN_DuiStringGetData g_pfnDuiStringGetData;

// ==========================================
// Helper Functions (vtable-based)
// ==========================================
const wchar_t* DuiLib_GetControlClass(void* pControl);
const wchar_t* DuiLib_GetControlName(void* pControl, wchar_t* pBuf, int bufLen);
RECT DuiLib_GetControlPos(void* pControl);
void* DuiLib_GetContainerItemAt(void* pControl, int index);
void* DuiLib_GetControlParent(void* pControl);
int DuiLib_GetContainerCount(void* pControl);

// ==========================================
// IAccessible Wrapper Class
// ==========================================
class MyAccessibleImpl;

// Get or create Accessible wrapper
IDispatch* GetOrCreateAccessibleWrapper(HWND hwnd, void* pPM, void* pControl, void* pParentControl);

// Clear cache
void ClearAccessibleCache(void* pControl);

// Initialize Accessible module
void InitAccessibleModule();

// Shutdown Accessible module (stops cleanup thread and releases cache)
void ShutdownAccessibleModule();
