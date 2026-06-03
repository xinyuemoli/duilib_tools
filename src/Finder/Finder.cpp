#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include "../../include/IPC.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")

// Button IDs
#define ID_BTN_INJECT    1
#define ID_BTN_DETECT   2
#define ID_LIST       0

static HWND g_hwndProcessList = NULL;
static HWND g_hwndInjectBtn = NULL;
static HWND g_hwndDetectBtn = NULL;
static HWND g_hwndResultStatic = NULL;
static DWORD g_dwTargetPID = 0;
static HANDLE g_hPipe = NULL;
static BOOL g_bCapturing = FALSE;  // Spy++ style capture mode

void RefreshProcessList();

BOOL InjectDLL(DWORD dwPID, const char* szDllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPID);
    if (!hProcess) {
        DWORD dwErr = GetLastError();
        WCHAR szErr[256];
        wsprintf(szErr, L"Cannot open process. Error: %lu", dwErr);
        MessageBox(NULL, szErr, L"Error", MB_ICONERROR);
        return FALSE;
    }

    size_t dwSize = lstrlenA(szDllPath) + 1;
    LPVOID pRemoteBuf = VirtualAllocEx(hProcess, NULL, dwSize, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteBuf) {
        CloseHandle(hProcess);
        MessageBox(NULL, L"Memory allocation failed", L"Error", MB_ICONERROR);
        return FALSE;
    }

    WriteProcessMemory(hProcess, pRemoteBuf, (LPVOID)szDllPath, dwSize, NULL);

    LPVOID pLoadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!pLoadLib) {
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        MessageBox(NULL, L"Cannot find LoadLibraryA", L"Error", MB_ICONERROR);
        return FALSE;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pLoadLib, pRemoteBuf, 0, NULL);

    BOOL bRet = FALSE;
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        Sleep(500);
        HANDLE hPipe = CreateFileA("\\\\.\\pipe\\DuiLibFinderPipe",
            GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe != INVALID_HANDLE_VALUE) {
            bRet = TRUE;
            CloseHandle(hPipe);
        } else {
            MessageBox(NULL, L"DLL loaded but pipe not created. Check detours dependency.", L"Warning", MB_OK);
            bRet = TRUE;
        }
        CloseHandle(hThread);
    } else {
        DWORD dwErr = GetLastError();
        WCHAR szErr[256];
        wsprintf(szErr, L"Create remote thread failed. Error: %lu", dwErr);
        MessageBox(NULL, szErr, L"Error", MB_ICONERROR);
    }

    VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return bRet;
}

void RefreshProcessList() {
    SendMessage(g_hwndProcessList, LB_RESETCONTENT, 0, 0);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (hProcess) {
                HMODULE hMods[1024];
                DWORD cbNeeded;
                if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
                    for (DWORD i = 0; i < cbNeeded / sizeof(HMODULE); i++) {
                        char szModName[MAX_PATH];
                        GetModuleBaseNameA(hProcess, hMods[i], szModName, sizeof(szModName));
                        if (_stricmp(szModName, "duilib.dll") == 0 ||
                            _stricmp(szModName, "Duilib.dll") == 0) {
                            WCHAR szItem[MAX_PATH];
                            wsprintf(szItem, L"%ls (PID: %u)", pe.szExeFile, pe.th32ProcessID);
                            int idx = SendMessage(g_hwndProcessList, LB_ADDSTRING, 0, (LPARAM)szItem);
                            SendMessage(g_hwndProcessList, LB_SETITEMDATA, idx, pe.th32ProcessID);
                            break;
                        }
                    }
                }
                CloseHandle(hProcess);
            }
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
}

// Query control at screen coordinates
void QueryControl(POINT pt) {
    if (!g_hPipe || g_hPipe == INVALID_HANDLE_VALUE) return;

    FinderRequest req;
    req.cmd = CMD_QUERY_CONTROL;
    req.pt = pt;

    FinderResponse resp = {0};

    DWORD dwWrite = 0, dwRead = 0;
    WriteFile(g_hPipe, &req, sizeof(req), &dwWrite, NULL);
    ReadFile(g_hPipe, &resp, sizeof(resp), &dwRead, NULL);

    WCHAR wbuf[256] = L"";
    if (resp.result == 0) {
        lstrcpyW(wbuf, L"Class: ");
        lstrcatW(wbuf, resp.className);
        lstrcatW(wbuf, L" | Name: ");
        lstrcatW(wbuf, resp.name);
    } else if (resp.result == 1) {
        lstrcpyW(wbuf, L"Control not found");
    } else {
        lstrcpyW(wbuf, L"Error: Cannot get control info");
    }
    SetWindowText(g_hwndResultStatic, wbuf);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == ID_BTN_INJECT) {
            int idx = SendMessage(g_hwndProcessList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                g_dwTargetPID = SendMessage(g_hwndProcessList, LB_GETITEMDATA, idx, 0);

                char dllPath[MAX_PATH];
                GetCurrentDirectoryA(MAX_PATH, dllPath);
                lstrcatA(dllPath, "\\HookDll.dll");

                WCHAR wszPath[MAX_PATH];
                MultiByteToWideChar(CP_ACP, 0, dllPath, -1, wszPath, MAX_PATH);

                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(wszPath, &fd);
                if (hFind == INVALID_HANDLE_VALUE) {
                    MessageBox(hwnd, L"DLL not found!", L"Error", MB_ICONERROR);
                    return 0;
                }
                FindClose(hFind);

                if (InjectDLL(g_dwTargetPID, dllPath)) {
                    MessageBox(hwnd, L"Inject success! Click 'Start Detect' then click on target window", L"Info", MB_OK);
                }
            }
        } 
        break;
    }
        
    // Spy++ style capture: all mouse msgs sent here after SetCapture
    case WM_LBUTTONDOWN:
    {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        
        // 1.
        RECT rcBtn;
        GetWindowRect(g_hwndDetectBtn, &rcBtn); 
        
        // 2. 
        POINT ptMouse = {x, y};
        ClientToScreen(hwnd, &ptMouse); 
        
        // 3. 
        if (PtInRect(&rcBtn, ptMouse)) 
        {
            // 4. 
            g_bCapturing = TRUE;
            SetCapture(hwnd); 
            SetCursor(LoadCursorW(NULL, IDC_CROSS));
            return 0; 
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (g_bCapturing) {
            g_bCapturing = FALSE;
            ReleaseCapture();

            POINT pt;
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            // Client to screen coords
            ClientToScreen(hwnd, &pt);

            QueryControl(pt);
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        break;
    }

    case WM_CANCELMODE:
    {
        if (g_bCapturing) {
            g_bCapturing = FALSE;
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        break;
    }
        
    case WM_CLOSE:
    {
        if (g_hPipe) CloseHandle(g_hPipe);
        DestroyWindow(hwnd);
        PostQuitMessage(0);
        break;
    }
        
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int main() {
    HINSTANCE hInstance = GetModuleHandleW(NULL);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DuiLibFinderClass";
    RegisterClassW(&wc);

    MSG msg;
    HWND hwnd = CreateWindowW(
        L"DuiLibFinderClass", L"DuiLib Control Finder",
        WS_CAPTION | WS_SYSMENU,
        100, 100, 400, 320,
        NULL, NULL, hInstance, NULL);

    g_hwndProcessList = CreateWindowW(L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | LBS_STANDARD,
        20, 20, 360, 150, hwnd, (HMENU)ID_LIST, hInstance, NULL);
    g_hwndInjectBtn = CreateWindowW(L"BUTTON", L"Inject DLL",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 180, 100, 30, hwnd, (HMENU)ID_BTN_INJECT, hInstance, NULL);
    g_hwndDetectBtn = CreateWindowW(L"BUTTON", L"Start Detect",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, 180, 100, 30, hwnd, (HMENU)ID_BTN_DETECT, hInstance, NULL);
    g_hwndResultStatic = CreateWindowW(L"STATIC", L"Waiting for detection...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 220, 360, 30, hwnd, NULL, hInstance, NULL);

    RefreshProcessList();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}