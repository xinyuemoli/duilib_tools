#pragma once
#include <windows.h>
#include <oleacc.h>
#include <map>

// ==========================================
// DuiLib Function Pointer Types
// ==========================================
typedef const wchar_t* (__thiscall* FN_GetClass)(void* pControl);
typedef const wchar_t* (__thiscall* FN_GetName)(void* pControl);
typedef RECT           (__thiscall* FN_GetPos)(void* pControl);
typedef int            (__thiscall* FN_GetCount)(void* pControl);
typedef void*          (__thiscall* FN_GetItemAt)(void* pControl, int index);
typedef void*          (__thiscall* FN_GetParent)(void* pControl);
typedef void*          (__thiscall* FN_GetRoot)(void* pPM);
typedef HWND           (__thiscall* FN_GetPaintWindow)(void* pPM);

// Global DuiLib function pointers
extern FN_GetClass       g_pfnGetClass;
extern FN_GetName        g_pfnGetName;
extern FN_GetPos         g_pfnGetPos;
extern FN_GetCount     g_pfnGetCount;
extern FN_GetItemAt   g_pfnGetItemAt;
extern FN_GetParent   g_pfnGetParent;
extern FN_GetRoot     g_pfnGetRoot;
extern FN_GetPaintWindow g_pfnGetPaintWindow;
extern BOOL g_bAccessibleInitialized;

// Safe helper functions
const wchar_t* DuiLib_GetControlClass(void* pControl);
const wchar_t* DuiLib_GetControlName(void* pControl);
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

// Initialize Accessible module (bind DuiLib symbols)
void InitAccessibleModule();