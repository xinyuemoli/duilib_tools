#include "Accessible.h"
#include "../duilib/ModuleFinder.h"
#include <string>
#include <excpt.h>

#pragma comment(lib, "oleacc.lib")

#ifndef NAVDIR_PARENT
#define NAVDIR_PARENT 0x0007
#endif
#ifndef STATE_SYSTEM_NORMAL
#define STATE_SYSTEM_NORMAL 0
#endif

// ==========================================
// Global Variables
// ==========================================
std::map<void*, MyAccessibleImpl*> g_accessibleMap;
CRITICAL_SECTION g_csAccessibleMap;
BOOL g_bAccessibleInitialized = FALSE;

FN_GetRoot        g_pfnGetRoot        = nullptr;
FN_GetPaintWindow g_pfnGetPaintWindow = nullptr;

// CDuiString::GetData - non-virtual, resolved via GetProcAddress
typedef const wchar_t* (__thiscall* FN_DuiStringGetData)(void* pDuiString);
FN_DuiStringGetData g_pfnDuiStringGetData = nullptr;

// CControlUI::GetName - virtual, returns CDuiString by value.
// MSVC __thiscall with return-by-value: hidden ret* is first stack param.
// Actual ABI: GetName(CDuiString* retBuf, CControlUI* this)
typedef void* (__thiscall* FN_VirtualGetName)(void* pControl, void* pRetBuf);
static const int VT_GETNAME = 5;

// ==========================================
// Vtable indices (verified via IDA)
// ==========================================
// CControlUI vtable (object offset 0):
//   [5]  GetName   [7]  GetClass   [17] GetParent   [22] GetPos
// IContainerUI vtable (object offset 0x418, CContainerUI only):
//   [0]  GetItemAt  [3]  GetCount

static const int VT_GETCLASS  = 7;
static const int VT_GETPARENT = 17;
static const int VT_GETPOS    = 22;
static const int VT_GETCOUNT  = 3;   // IContainerUI vtable
static const int VT_GETITEMAT = 0;   // IContainerUI vtable
static const int OFF_ICONTAINER_VT = 0x418; // 1048, verified in CVerticalLayoutUI/CHorizontalLayoutUI/CTabLayoutUI constructors

// ==========================================
// Helper Functions — vtable calls with SEH
// ==========================================
const wchar_t* DuiLib_GetControlClass(void* pControl) {
    if (!pControl) return L"CControlUI";
    __try {
        void** vt = *(void***)pControl;
        if (!vt) return L"CControlUI";
        typedef const wchar_t* (__thiscall* FN)(void*);
        FN pfn = (FN)vt[VT_GETCLASS];
        if (!pfn) return L"CControlUI";
        const wchar_t* cls = pfn(pControl);
        return cls ? cls : L"CControlUI";
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[Acc] GetControlClass SEH\n");
        return L"CControlUI";
    }
}

// Get control name, copied into caller-provided buffer.
// Returns pBuf on success, L"" on failure.
const wchar_t* DuiLib_GetControlName(void* pControl, wchar_t* pBuf, int bufLen) {
    if (!pBuf || bufLen <= 0) return L"";
    pBuf[0] = L'\0';
    if (!pControl || !g_pfnDuiStringGetData) return L"";
    __try {
        void** vt = *(void***)pControl;
        if (!vt) return L"";
        FN_VirtualGetName pfnGetName = (FN_VirtualGetName)vt[VT_GETNAME];
        if (!pfnGetName) return L"";
        char cdStringBuf[40] = {};
        pfnGetName(pControl, cdStringBuf);
        const wchar_t* pStr = g_pfnDuiStringGetData(cdStringBuf);
        if (pStr && pStr[0]) {
            wcsncpy_s(pBuf, bufLen, pStr, _TRUNCATE);
        }
        // cdStringBuf destructor runs here — safe because we already copied
        return pBuf;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[Acc] DuiLib_GetControlName SEH\n");
        return L"";
    }
}

RECT DuiLib_GetControlPos(void* pControl) {
    RECT r = {0, 0, 0, 0};
    if (!pControl) return r;
    __try {
        void** vt = *(void***)pControl;
        if (!vt) return r;
        typedef const RECT* (__thiscall* FN)(void*);
        FN pfn = (FN)vt[VT_GETPOS];
        if (!pfn) return r;
        const RECT* prc = pfn(pControl);
        if (prc) r = *prc;
        return r;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[Acc] GetControlPos SEH\n");
        return r;
    }
}

void* DuiLib_GetControlParent(void* pControl) {
    if (!pControl) return nullptr;
    __try {
        void** vt = *(void***)pControl;
        if (!vt) return nullptr;
        typedef void* (__thiscall* FN)(void*);
        FN pfn = (FN)vt[VT_GETPARENT];
        if (!pfn) return nullptr;
        return pfn(pControl);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[Acc] GetControlParent SEH\n");
        return nullptr;
    }
}

static bool IsContainerUI(void* pControl) {
    // All IContainerUI implementors have these keywords in class name:
    // Layout, Container, List, Combo, Header, Menu, Tree, RichEdit
    const wchar_t* cls = DuiLib_GetControlClass(pControl);
    return wcsstr(cls, L"Layout") || wcsstr(cls, L"Container") ||
           wcsstr(cls, L"List")   || wcsstr(cls, L"Combo")    ||
           wcsstr(cls, L"Header") || wcsstr(cls, L"Menu")     ||
           wcsstr(cls, L"Tree")   || wcsstr(cls, L"RichEdit");
}

int DuiLib_GetContainerCount(void* pControl) {
    if (!pControl || !IsContainerUI(pControl)) return 0;
    __try {
        // IContainerUI subobject starts at offset 0x418 in the full object.
        // IContainerUI functions expect 'this' = subobject pointer.
        void* icaThis = (char*)pControl + OFF_ICONTAINER_VT;
        void** icaVt = *(void***)icaThis;
        typedef int (__thiscall* FN)(void*);
        FN pfn = (FN)icaVt[VT_GETCOUNT];
        char buf[256];
        sprintf_s(buf, "[Acc] GetContainerCount: ctrl=%p icaThis=%p icaVt=%p pfn=%p\n",
            pControl, icaThis, icaVt, pfn);
        OutputDebugStringA(buf);
        if (!pfn) return 0;
        int count = pfn(icaThis);
        sprintf_s(buf, "[Acc] GetContainerCount: result=%d\n", count);
        OutputDebugStringA(buf);
        return count;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[Acc] GetContainerCount SEH\n");
        return 0;
    }
}

void* DuiLib_GetContainerItemAt(void* pControl, int index) {
    if (!pControl || index < 0 || !IsContainerUI(pControl)) return nullptr;
    __try {
        void* icaThis = (char*)pControl + OFF_ICONTAINER_VT;
        void** icaVt = *(void***)icaThis;
        typedef void* (__thiscall* FN)(void*, int);
        FN pfn = (FN)icaVt[VT_GETITEMAT];
        if (!pfn) return nullptr;
        return pfn(icaThis, index);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[Acc] GetContainerItemAt SEH\n");
        return nullptr;
    }
}

// ==========================================
// IAccessible Implementation
// ==========================================
class MyAccessibleImpl : public IAccessible {
private:
    LONG  m_refCount;
    HWND  m_hWnd;
    void* m_pPM;
    void* m_pControl;
    void* m_pParentControl;

public:
    MyAccessibleImpl(HWND hwnd, void* pPM, void* pControl, void* pParent = nullptr)
        : m_refCount(1), m_hWnd(hwnd), m_pPM(pPM), m_pControl(pControl), m_pParentControl(pParent) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
            *ppvObject = static_cast<IAccessible*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() {
        return InterlockedIncrement(&m_refCount);
    }

    STDMETHODIMP_(ULONG) Release() {
        LONG res = InterlockedDecrement(&m_refCount);
        if (res == 0) delete this;
        return res;
    }

    // IDispatch
    STDMETHODIMP GetTypeInfoCount(UINT* pctinfo) {
        if (pctinfo) *pctinfo = 0;
        return S_OK;
    }
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) { return E_NOTIMPL; }

    // Navigation
    STDMETHODIMP get_accChildCount(LONG* pcountChildren) {
        if (!pcountChildren) return E_POINTER;
        int count = DuiLib_GetContainerCount(m_pControl);
        *pcountChildren = count;
        const wchar_t* cls = DuiLib_GetControlClass(m_pControl);
        char buf[256];
        sprintf_s(buf, "[Acc] get_accChildCount: ctrl=%p class=%ws count=%d\n", m_pControl, cls, count);
        OutputDebugStringA(buf);
        return S_OK;
    }

    STDMETHODIMP get_accChild(VARIANT varChild, IDispatch** ppdispChild) {
        if (!ppdispChild) return E_POINTER;
        *ppdispChild = NULL;
        if (varChild.vt == VT_I4 && varChild.lVal > 0) {
            int index = varChild.lVal - 1;
            void* pChild = DuiLib_GetContainerItemAt(m_pControl, index);
            const wchar_t* childCls = pChild ? DuiLib_GetControlClass(pChild) : L"(null)";
            char buf[256];
            sprintf_s(buf, "[Acc] get_accChild: parent=%p childId=%d child=%p class=%ws\n",
                m_pControl, varChild.lVal, pChild, childCls);
            OutputDebugStringA(buf);
            if (pChild) {
                *ppdispChild = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, pChild, m_pControl);
                if (*ppdispChild) return S_OK;
            }
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP accNavigate(LONG navDir, VARIANT varStart, VARIANT* pvarEnd) {
        if (!pvarEnd) return E_POINTER;
        pvarEnd->vt = VT_EMPTY;

        if (varStart.vt != VT_I4 || varStart.lVal != CHILDID_SELF)
            return DISP_E_MEMBERNOTFOUND;

        const wchar_t* cls = DuiLib_GetControlClass(m_pControl);
        char buf[256];
        sprintf_s(buf, "[Acc] accNavigate: ctrl=%p class=%ws dir=%d\n", m_pControl, cls, navDir);
        OutputDebugStringA(buf);

        if (navDir == NAVDIR_FIRSTCHILD) {
            int count = DuiLib_GetContainerCount(m_pControl);
            if (count > 0) {
                void* pChild = DuiLib_GetContainerItemAt(m_pControl, 0);
                sprintf_s(buf, "[Acc] NAVDIR_FIRSTCHILD: child=%p\n", pChild);
                OutputDebugStringA(buf);
                if (pChild) {
                    pvarEnd->vt = VT_DISPATCH;
                    pvarEnd->pdispVal = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, pChild, m_pControl);
                    return S_OK;
                }
            }
        }
        else if (navDir == NAVDIR_PARENT) {
            sprintf_s(buf, "[Acc] NAVDIR_PARENT: parent=%p\n", m_pParentControl);
            OutputDebugStringA(buf);
            if (m_pParentControl) {
                pvarEnd->vt = VT_DISPATCH;
                pvarEnd->pdispVal = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, m_pParentControl, nullptr);
                return S_OK;
            }
        }
        else if (navDir == NAVDIR_NEXT || navDir == NAVDIR_PREVIOUS) {
            void* pParent = DuiLib_GetControlParent(m_pControl);
            sprintf_s(buf, "[Acc] NAVDIR_NEXT/PREV: parent=%p\n", pParent);
            OutputDebugStringA(buf);
            if (pParent) {
                int count = DuiLib_GetContainerCount(pParent);
                for (int i = 0; i < count; i++) {
                    void* sibling = DuiLib_GetContainerItemAt(pParent, i);
                    if (sibling == m_pControl) {
                        int targetIdx = (navDir == NAVDIR_NEXT) ? i + 1 : i - 1;
                        sprintf_s(buf, "[Acc] NAVDIR sibling: myIdx=%d targetIdx=%d count=%d\n", i, targetIdx, count);
                        OutputDebugStringA(buf);
                        if (targetIdx >= 0 && targetIdx < count) {
                            void* pTarget = DuiLib_GetContainerItemAt(pParent, targetIdx);
                            if (pTarget) {
                                pvarEnd->vt = VT_DISPATCH;
                                pvarEnd->pdispVal = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, pTarget, pParent);
                                return S_OK;
                            }
                        }
                        break;
                    }
                }
            }
        }

        return DISP_E_MEMBERNOTFOUND;
    }

    // Properties
    STDMETHODIMP get_accName(VARIANT varChild, BSTR* pszName) {
        if (!pszName) return E_POINTER;
        if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
            wchar_t nameBuf[256] = {};
            DuiLib_GetControlName(m_pControl, nameBuf, 256);
            const wchar_t* cls = DuiLib_GetControlClass(m_pControl);
            *pszName = SysAllocString(nameBuf[0] ? nameBuf : cls);
            char logBuf[256];
            sprintf_s(logBuf, "[Acc] get_accName: ctrl=%p class=%ws name=%ws\n",
                m_pControl, cls, nameBuf);
            OutputDebugStringA(logBuf);
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP get_accRole(VARIANT varChild, VARIANT* pvarRole) {
        if (!pvarRole) return E_POINTER;
        pvarRole->vt = VT_I4;
        if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
            const wchar_t* pClass = DuiLib_GetControlClass(m_pControl);
            if (wcsstr(pClass, L"Button") || wcsstr(pClass, L"Option")) pvarRole->lVal = ROLE_SYSTEM_PUSHBUTTON;
            else if (wcsstr(pClass, L"Edit")) pvarRole->lVal = ROLE_SYSTEM_TEXT;
            else if (wcsstr(pClass, L"Combo")) pvarRole->lVal = ROLE_SYSTEM_COMBOBOX;
            else if (wcsstr(pClass, L"List")) pvarRole->lVal = ROLE_SYSTEM_LIST;
            else if (wcsstr(pClass, L"Tree")) pvarRole->lVal = ROLE_SYSTEM_OUTLINE;
            else if (wcsstr(pClass, L"Label") || wcsstr(pClass, L"Text")) pvarRole->lVal = ROLE_SYSTEM_STATICTEXT;
            else if (wcsstr(pClass, L"Layout") || wcsstr(pClass, L"Container")) pvarRole->lVal = ROLE_SYSTEM_GROUPING;
            else pvarRole->lVal = ROLE_SYSTEM_CLIENT;
            return S_OK;
        }
        pvarRole->lVal = ROLE_SYSTEM_CLIENT;
        return S_OK;
    }

    STDMETHODIMP get_accState(VARIANT varChild, VARIANT* pvarState) {
        if (!pvarState) return E_POINTER;
        pvarState->vt = VT_I4;
        pvarState->lVal = STATE_SYSTEM_NORMAL;
        return S_OK;
    }

    STDMETHODIMP get_accValue(VARIANT varChild, BSTR* pszValue) {
        if (!pszValue) return E_POINTER;
        if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
            *pszValue = SysAllocString(DuiLib_GetControlClass(m_pControl));
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP accLocation(LONG* pxLeft, LONG* pyTop, LONG* pcxWidth, LONG* pcyHeight, VARIANT varChild) {
        if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight) return E_POINTER;
        if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
            RECT rc = DuiLib_GetControlPos(m_pControl);
            if (m_hWnd) {
                POINT pt = { rc.left, rc.top };
                ClientToScreen(m_hWnd, &pt);
                *pxLeft = pt.x;
                *pyTop = pt.y;
                pt = { rc.right, rc.bottom };
                ClientToScreen(m_hWnd, &pt);
                *pcxWidth = pt.x - *pxLeft;
                *pcyHeight = pt.y - *pyTop;
            } else {
                *pxLeft = rc.left;
                *pyTop = rc.top;
                *pcxWidth = rc.right - rc.left;
                *pcyHeight = rc.bottom - rc.top;
            }
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP get_accParent(IDispatch** ppdispParent) {
        if (!ppdispParent) return E_POINTER;
        *ppdispParent = NULL;
        char buf[256];
        sprintf_s(buf, "[Acc] get_accParent: ctrl=%p parent=%p\n", m_pControl, m_pParentControl);
        OutputDebugStringA(buf);
        if (m_pParentControl) {
            *ppdispParent = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, m_pParentControl, nullptr);
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    // Stubs
    STDMETHODIMP get_accDescription(VARIANT, BSTR*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP get_accHelp(VARIANT, BSTR*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP get_accHelpTopic(BSTR*, VARIANT, LONG*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP get_accKeyboardShortcut(VARIANT, BSTR*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP get_accFocus(VARIANT*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP get_accSelection(VARIANT*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP get_accDefaultAction(VARIANT, BSTR*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP accSelect(LONG, VARIANT) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP accHitTest(LONG, LONG, VARIANT*) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP accDoDefaultAction(VARIANT) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP put_accName(VARIANT, BSTR) { return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP put_accValue(VARIANT, BSTR) { return DISP_E_MEMBERNOTFOUND; }
};

// ==========================================
// Wrapper Factory
// ==========================================
IDispatch* GetOrCreateAccessibleWrapper(HWND hwnd, void* pPM, void* pControl, void* pParentControl) {
    if (!pControl || !g_bAccessibleInitialized)
        return nullptr;

    EnterCriticalSection(&g_csAccessibleMap);

    auto it = g_accessibleMap.find(pControl);
    if (it != g_accessibleMap.end()) {
        it->second->AddRef();
        LeaveCriticalSection(&g_csAccessibleMap);
        return static_cast<IDispatch*>(it->second);
    }

    MyAccessibleImpl* pNewAcc = new MyAccessibleImpl(hwnd, pPM, pControl, pParentControl);
    g_accessibleMap[pControl] = pNewAcc;
    pNewAcc->AddRef();
    const wchar_t* cls = DuiLib_GetControlClass(pControl);
    char buf[256];
    sprintf_s(buf, "[Acc] CreateWrapper: ctrl=%p class=%ws cache=%d\n",
        pControl, cls, (int)g_accessibleMap.size());
    OutputDebugStringA(buf);

    LeaveCriticalSection(&g_csAccessibleMap);

    return static_cast<IDispatch*>(pNewAcc);
}

void ClearAccessibleCache(void* pControl) {
    EnterCriticalSection(&g_csAccessibleMap);
    auto it = g_accessibleMap.find(pControl);
    if (it != g_accessibleMap.end()) {
        it->second->Release();
        g_accessibleMap.erase(it);
    }
    LeaveCriticalSection(&g_csAccessibleMap);
}

// ==========================================
// Initialization
// ==========================================
void InitAccessibleModule() {
    InitializeCriticalSection(&g_csAccessibleMap);

    // Only resolve non-virtual functions via GetProcAddress
    // Virtual functions (GetClass, GetParent, GetPos, GetCount, GetItemAt)
    // are called through vtable indices — no export resolution needed
    g_pfnGetRoot         = (FN_GetRoot)         FindDuiLibExport("?GetRoot@CPaintManagerUI@DuiLib@@QBEPAVCControlUI@2@XZ");
    g_pfnGetPaintWindow  = (FN_GetPaintWindow)  FindDuiLibExport("?GetPaintWindow@CPaintManagerUI@DuiLib@@UBEPAUHWND__@@XZ");
    g_pfnDuiStringGetData = (FN_DuiStringGetData) FindDuiLibExport("?GetData@CDuiString@DuiLib@@QBEPB_WXZ");

    g_bAccessibleInitialized = TRUE;
    OutputDebugStringA("[Accessible] Module initialized (vtable mode)\n");
}
