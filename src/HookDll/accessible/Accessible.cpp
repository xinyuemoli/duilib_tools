#include "Accessible.h"
#include "../duilib/ModuleFinder.h"
#include <string>

#pragma comment(lib, "oleacc.lib")

// ==========================================
// Global Variables
// ==========================================
std::map<void*, MyAccessibleImpl*> g_accessibleMap;
CRITICAL_SECTION g_csAccessibleMap;

FN_GetClass       g_pfnGetClass       = nullptr;
FN_GetName        g_pfnGetName        = nullptr;
FN_GetPos        g_pfnGetPos        = nullptr;
FN_GetCount     g_pfnGetCount     = nullptr;
FN_GetItemAt   g_pfnGetItemAt   = nullptr;
FN_GetParent   g_pfnGetParent   = nullptr;
FN_GetRoot     g_pfnGetRoot     = nullptr;
FN_GetPaintWindow g_pfnGetPaintWindow = nullptr;

// ==========================================
// Helper Functions
// ==========================================
const wchar_t* DuiLib_GetControlClass(void* pControl) {
    return g_pfnGetClass ? g_pfnGetClass(pControl) : L"CControlUI";
}

const wchar_t* DuiLib_GetControlName(void* pControl) {
    return g_pfnGetName ? g_pfnGetName(pControl) : L"";
}

RECT DuiLib_GetControlPos(void* pControl) {
    if (g_pfnGetPos) return g_pfnGetPos(pControl);
    RECT r = {0, 0, 0, 0};
    return r;
}

void* DuiLib_GetContainerItemAt(void* pControl, int index) {
    return g_pfnGetItemAt ? g_pfnGetItemAt(pControl, index) : nullptr;
}

void* DuiLib_GetControlParent(void* pControl) {
    return g_pfnGetParent ? g_pfnGetParent(pControl) : nullptr;
}

int DuiLib_GetContainerCount(void* pControl) {
    const wchar_t* pClass = DuiLib_GetControlClass(pControl);
    // Simple containment check
    if (wcsstr(pClass, L"Layout") || wcsstr(pClass, L"Container") || wcsstr(pClass, L"ListUI")) {
        if (g_pfnGetCount) return g_pfnGetCount(pControl);
    }
    return 0;
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

    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) {
        return E_NOTIMPL;
    }

    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) {
        return E_NOTIMPL;
    }

    STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
        return E_NOTIMPL;
    }

    // Navigation
    STDMETHODIMP get_accChildCount(LONG* pcountChildren) {
        if (!pcountChildren) return E_POINTER;
        *pcountChildren = DuiLib_GetContainerCount(m_pControl);
        return S_OK;
    }

    STDMETHODIMP get_accChild(VARIANT, IDispatch** ppdispChild) {
        if (!ppdispChild) return E_POINTER;
        *ppdispChild = NULL;
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP accNavigate(LONG navDir, VARIANT varStart, VARIANT* pvarEnd) {
        if (!pvarEnd) return E_POINTER;
        VariantInit(pvarEnd);

        if (varStart.vt == VT_I4 && varStart.lVal == CHILDID_SELF) {
            if (navDir == NAVDIR_FIRSTCHILD) {
                if (DuiLib_GetContainerCount(m_pControl) > 0) {
                    void* pChild = DuiLib_GetContainerItemAt(m_pControl, 0);
                    if (pChild) {
                        pvarEnd->vt = VT_DISPATCH;
                        pvarEnd->pdispVal = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, pChild, m_pControl);
                        return S_OK;
                    }
                }
            }
            else if (navDir == NAVDIR_NEXT) {
                if (m_pParentControl) {
                    int scount = DuiLib_GetContainerCount(m_pParentControl);
                    for (int i = 0; i < scount; ++i) {
                        if (DuiLib_GetContainerItemAt(m_pParentControl, i) == m_pControl) {
                            if (i + 1 < scount) {
                                void* pNext = DuiLib_GetContainerItemAt(m_pParentControl, i + 1);
                                pvarEnd->vt = VT_DISPATCH;
                                pvarEnd->pdispVal = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, pNext, m_pParentControl);
                                return S_OK;
                            }
                            break;
                        }
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
            const wchar_t* pName = DuiLib_GetControlName(m_pControl);
            *pszName = SysAllocString((pName && wcslen(pName) > 0) ? pName : L"DuiLib_Control");
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP get_accRole(VARIANT varChild, VARIANT* pvarRole) {
        if (!pvarRole) return E_POINTER;
        pvarRole->vt = VT_I4;
        pvarRole->lVal = ROLE_SYSTEM_CLIENT;
        if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
            const wchar_t* pClass = DuiLib_GetControlClass(m_pControl);
            if (pClass[0] == L'B' || pClass[0] == L'O') { // ButtonUI, OptionUI
                pvarRole->lVal = ROLE_SYSTEM_PUSHBUTTON;
            }
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP get_accState(VARIANT varChild, VARIANT* pvarState) {
        if (!pvarState) return E_POINTER;
        pvarState->vt = VT_I4;
        pvarState->lVal = 0;  // STATE_SYSTEM_NORMAL = 0
        return S_OK;
    }

    // Location
    STDMETHODIMP accLocation(LONG* pxLeft, LONG* pyTop, LONG* pcxWidth, LONG* pcyHeight, VARIANT varChild) {
        if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight) return E_POINTER;
        if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
            RECT rc = DuiLib_GetControlPos(m_pControl);
            POINT pt = { rc.left, rc.top };
            ::ClientToScreen(m_hWnd, &pt);
            *pxLeft = pt.x;
            *pyTop = pt.y;
            *pcxWidth = rc.right - rc.left;
            *pcyHeight = rc.bottom - rc.top;
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    // Remaining interfaces - stub implementations
    STDMETHODIMP get_accParent(IDispatch** ppdispParent) {
        if (!ppdispParent) return E_POINTER;
        *ppdispParent = NULL;
        if (m_pParentControl) {
            *ppdispParent = GetOrCreateAccessibleWrapper(m_hWnd, m_pPM, m_pParentControl, DuiLib_GetControlParent(m_pParentControl));
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }

    STDMETHODIMP get_accValue(VARIANT, BSTR*) { return DISP_E_MEMBERNOTFOUND; }
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
    if (!pControl) return nullptr;

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

    g_pfnGetClass       = (FN_GetClass)      FindDuiLibExport("?GetClass@CControlUI@DuiLib@@UBEPB_WXZ");
    g_pfnGetName        = (FN_GetName)       FindDuiLibExport("?GetName@CControlUI@DuiLib@@UBE?AVCDuiString@2@XZ");
    g_pfnGetPos         = (FN_GetPos)        FindDuiLibExport("?GetPos@CControlUI@DuiLib@@UBEABUtagRECT@@XZ");
    g_pfnGetCount     = (FN_GetCount)     FindDuiLibExport("?GetCount@CContainerUI@DuiLib@@UBEHXZ");
    g_pfnGetItemAt   = (FN_GetItemAt)    FindDuiLibExport("?GetItemAt@CContainerUI@DuiLib@@UBEPAVCControlUI@2@H@Z");
    g_pfnGetParent   = (FN_GetParent)   FindDuiLibExport("?GetParent@CControlUI@DuiLib@@UBEPAV12@XZ");
    g_pfnGetRoot     = (FN_GetRoot)    FindDuiLibExport("?GetRoot@CPaintManagerUI@DuiLib@@QBEPAVCControlUI@2@XZ");
    g_pfnGetPaintWindow = (FN_GetPaintWindow) FindDuiLibExport("?GetPaintWindow@CPaintManagerUI@DuiLib@@UBEPAUHWND__@@XZ");

    OutputDebugStringA("[Accessible] Module initialized\n");
}