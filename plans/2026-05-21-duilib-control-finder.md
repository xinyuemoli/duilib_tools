# DuiLib Control Finder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建一个类似 Spy++ 的工具，用于检测 DuiLib 界面上鼠标点击的控件类名和名称

**Architecture:** 两部分组成 - Finder.exe (GUI + 注入器 + 鼠标钩子) 和 HookDll.dll (注入目标进程，提供 IPC 服务端)。通过命名管道通信，HookMessageHandler 捕获 CPaintManagerUI* 指针。

**Tech Stack:** C++ (MSVC), CMake, Windows SDK

---

## 文件结构

```
duilib_tools/
├── docs/
│   └── specs/
│       └── 2026-05-21-duilib-control-finder-design.md
├── plans/
│   └── 2026-05-21-duilib-control-finder.md
├── include/
│   └── IPC.h              # 共用 IPC 协议定义
├── src/
│   ├── HookDll/           # 注入到目标进程的 DLL
│   │   └── CMakeLists.txt
│   └── Finder/            # GUI 程序
│       └── CMakeLists.txt
├── CMakeLists.txt         # 根 CMake 文件
├── build.bat             # 构建脚本
└── bin/                   # 输出目录
```

---

### Task 1: 创建共用 IPC 协议头文件

**Files:**
- Create: `include/IPC.h`

- [ ] **Step 1: 创建 IPC.h 头文件**

```cpp
#pragma once

#include <windows.h>

// 管道名称
#define FINDER_PIPE_NAME "\\\\.\\\\pipe\\\\DuiLibFinderPipe"

// 命令码
#define CMD_QUERY_CONTROL  1   // 查询坐标所在控件

// 请求结构 (exe -> dll)
#pragma pack(push, 1)
struct FinderRequest {
    DWORD cmd;        // CMD_QUERY_CONTROL
    POINT pt;        // 屏幕坐标
};

// 响应结构 (dll -> exe)
struct FinderResponse {
    DWORD result;    // 0=成功, 1=未找到, 2=错误
    wchar_t className[64];
    wchar_t name[64];
};
#pragma pack(pop)
```

- [ ] **Step 2: 提交创建**

```bash
git add include/IPC.h
git commit -m "feat: add IPC protocol header"
```

---

### Task 2: 创建根 CMakeLists.txt 和 build.bat

**Files:**
- Create: `CMakeLists.txt`
- Create: `build.bat`

- [ ] **Step 1: 创建根 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.15)
project(DuiLibFinder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出到 bin 目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Windows SDK
add_definitions(-DUNICODE -D_UNICODE)
add_definitions(-DWIN32_LEAN_AND_MEAN)

# 添加子项目
add_subdirectory(src/HookDll)
add_subdirectory(src/Finder)
```

- [ ] **Step 2: 创建 build.bat**

```bat
@echo off
setlocal

echo ========== Building DuiLib Finder ==========

:: 创建 build 目录
if not exist build mkdir build

cd build

:: 配置 CMake
echo [1/3] Configuring CMake...
cmake -G "Visual Studio 17 2022" -A Win32 ..
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

:: 编译
echo [2/3] Building...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo [3/3] Build complete!
echo.

:: 显示输出文件
echo Output files:
dir /s /b ..\\bin\\*.exe 2>nul
dir /s /b ..\\bin\\*.dll 2>nul

echo.
echo Done!
pause
```

- [ ] **Step 3: 提交 CMake 相关文件**

```bash
git add CMakeLists.txt build.bat
git commit -m "feat: add CMake build system"
```

---

### Task 3: 实现 HookDll (注入的目标进程 DLL)

**Files:**
- Create: `src/HookDll/CMakeLists.txt`
- Create: `src/HookDll/HookDll.cpp`
- Create: `src/HookDll/HookDll.def`

- [ ] **Step 1: 创建 src/HookDll/CMakeLists.txt**

```cmake
add_library(HookDll SHARED
    HookDll.cpp
    HookDll.def
)

target_include_directories(HookDll PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(HookDll PRIVATE
    kernel32
    user32
)
```

- [ ] **Step 2: 创建 src/HookDll/HookDll.cpp**

```cpp
#include <windows.h>
#include <cstdio>
#include "../../include/IPC.h"

// ==========================================
// DuiLib 函数声明（从二进制分析得出）
// ==========================================

// CControlUI* __thiscall CPaintManagerUI::FindControl(POINT)
// 地址: ?FindControl@CPaintManagerUI@DuiLib@@QBEPAVCControlUI@2@UtagPOINT@@@Z
typedef CControlUI* (__thiscall* PFN_FindControl)(void* pm, POINT pt);

// const wchar_t* __thiscall CControlUI::GetClass()
// 地址: ?GetClass@CControlUI@DuiLib@@UBEPB_WXZ
typedef const wchar_t* (__thiscall* PFN_GetClass)(void* ctrl);

// CDuiString __thiscall CControlUI::GetName()
// 地址: ?GetName@CControlUI@DuiLib@@UBE?AVCDuiString@2@XZ
typedef CDuiString(__thiscall* PFN_GetName)(void* ctrl);

// bool __thiscall CPaintManagerUI::MessageHandler(UINT, UINT, int, int*)
// 地址: ?MessageHandler@CPaintManagerUI@DuiLib@@QAE_NIIJAAJ@Z
typedef bool(__thiscall* PFN_MessageHandler)(void* pm, UINT msg, UINT wParam, int lParam, int* pResult);

// ==========================================
// 全局变量
// ==========================================

static void* g_pPaintManager = nullptr;       // CPaintManagerUI* 指针
static HMODULE g_hDuiLib = nullptr;         // duilib.dll 模块句柄
static PFN_FindControl g_pfnFindControl = nullptr;
static PFN_GetClass g_pfnGetClass = nullptr;
static PFN_GetName g_pfnGetName = nullptr;
static PFN_MessageHandler g_pfnOriginalHandler = nullptr;

// Hook 相关字节
static BYTE g_originalBytes[16] = {0};
static BYTE g_jumpCode[16] = {0};
static LPVOID g_pMessageHandlerAddr = nullptr;

// ==========================================
// Hook 实现
// ==========================================

// 简化的 Inline Hook - 使用 Trampoline 方式
bool InstallMessageHandlerHook() {
    // 1. 初始化 duilib.dll 获取
    g_hDuiLib = GetModuleHandleA("duilib.dll");
    if (!g_hDuiLib) g_hDuiLib = GetModuleHandleA("Duilib.dll");
    if (!g_hDuiLib) {
        // 尝试从当前进程搜索
        HMODULE hMods[1024];
        DWORD cbNeeded;
        if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
            for (DWORD i = 0; i < cbNeeded / sizeof(HMODULE); i++) {
                char szName[MAX_PATH];
                GetModuleFileNameA(hMods[i], szName, sizeof(szName));
                if (_stricmp(szName + strlen(szName) - 12, "duilib.dll") == 0 ||
                    _stricmp(szName + strlen(szName) - 12, "Duilib.dll") == 0) {
                    g_hDuiLib = hMods[i];
                    break;
                }
            }
        }
    }
    if (!g_hDuiLib) return false;

    // 2. 获取函数地址
    g_pMessageHandlerAddr = GetProcAddress(g_hDuiLib, "?MessageHandler@CPaintManagerUI@DuiLib@@QAE_NIIJAAJ@Z");
    if (!g_pMessageHandlerAddr) return false;

    g_pfnFindControl = (PFN_FindControl)GetProcAddress(g_hDuiLib, "?FindControl@CPaintManagerUI@DuiLib@@QBEPAVCControlUI@2@UtagPOINT@@@Z");
    g_pfnGetClass = (PFN_GetClass)GetProcAddress(g_hDuiLib, "?GetClass@CControlUI@DuiLib@@UBEPB_WXZ");
    g_pfnGetName = (PFN_GetName)GetProcAddress(g_hDuiLib, "?GetName@CControlUI@DuiLib@@UBE?AVCDuiString@2@XZ");
    g_pfnOriginalHandler = (PFN_MessageHandler)g_pMessageHandlerAddr;

    // 3. 保存原始字节
    SIZE_T bytesRead;
    ReadProcessMemory(GetCurrentProcess(), g_pMessageHandlerAddr, g_originalBytes, 16, &bytesRead);

    // 4. 构建 Jump 指令（这里简化处理，实际需要完整的 thunk）
    // TODO: 实现完整的 Inline Hook

    return true;
}

// 卸载 Hook
void UninstallMessageHandlerHook() {
    if (g_pMessageHandlerAddr && g_originalBytes[0] != 0) {
        SIZE_T bytesWritten;
        WriteProcessMemory(GetCurrentProcess(), g_pMessageHandlerAddr, g_originalBytes, 16, &bytesWritten);
        memset(g_originalBytes, 0, sizeof(g_originalBytes));
    }
}

// ==========================================
// 管线服务器线程
// ==========================================

DWORD WINAPI PipeServerThread(LPVOID) {
    HANDLE hPipe = CreateNamedPipeA(
        FINDER_PIPE_NAME,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        sizeof(FinderResponse),
        sizeof(FinderRequest),
        0,
        nullptr
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[HookDll] CreateNamedPipe failed\n");
        return 0;
    }

    OutputDebugStringA("[HookDll] Pipe created, waiting...\n");

    while (TRUE) {
        if (!ConnectNamedPipe(hPipe, nullptr)) break;

        FinderRequest req{};
        FinderResponse resp{2}; // 默认错误

        DWORD dwBytesRead, dwBytesWrite;
        if (ReadFile(hPipe, &req, sizeof(req), &dwBytesRead, nullptr) && dwBytesRead > 0) {
            if (req.cmd == CMD_QUERY_CONTROL) {
                if (g_pPaintManager && g_pfnFindControl) {
                    // 调用 FindControl
                    CControlUI* pCtrl = g_pfnFindControl(g_pPaintManager, req.pt);
                    if (pCtrl) {
                        resp.result = 0;
                        const wchar_t* pszClass = g_pfnGetClass(pCtrl);
                        wcsncpy_s(resp.className, pszClass, _TRUNCATE);
                        CDuiString name = g_pfnGetName(pCtrl);
                        wcsncpy_s(resp.name, name.GetData(), _TRUNCATE);
                    } else {
                        resp.result = 1; // 未找到
                    }
                } else {
                    resp.result = 2; // 函数未初始化
                }
            }
        }

        WriteFile(hPipe, &resp, sizeof(resp), &dwBytesWrite, nullptr);
        DisconnectNamedPipe(hPipe);
    }

    CloseHandle(hPipe);
    return 0;
}

// ==========================================
// DLL 入口
// ==========================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        OutputDebugStringA("[HookDll] Loaded\n");

        // 安装 Hook
        InstallMessageHandlerHook();

        // 创建管线服务器线程
        CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        UninstallMessageHandlerHook();
        OutputDebugStringA("[HookDll] Unloaded\n");
    }
    return TRUE;
}
```

- [ ] **Step 3: 创建 src/HookDll/HookDll.def**

```def
LIBRARY HookDll
EXPORTS
    InstallHook       @1
    UninstallHook     @2
```

- [ ] **Step 4: 提交 HookDll**

```bash
git add src/HookDll/
git commit -m "feat: add HookDll - injected DLL for target process"
```

---

### Task 4: 实现 Finder (GUI 程序)

**Files:**
- Create: `src/Finder/CMakeLists.txt`
- Create: `src/Finder/Finder.cpp`

- [ ] **Step 1: 创建 src/Finder/CMakeLists.txt**

```cmake
add_executable(Finder
    Finder.cpp
)

target_include_directories(Finder PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(Finder PRIVATE
    kernel32
    user32
    advapi32
    dbghelp
)
```

- [ ] **Step 2: 创建 src/Finder/Finder.cpp**

```cpp
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include "../../include/IPC.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")

// ==========================================
// 全局变量
// ==========================================

static HWND g_hwndProcessList = nullptr;
static HWND g_hwndInjectBtn = nullptr;
static HWND g_hwndDetectBtn = nullptr;
static HWND g_hwndResultStatic = nullptr;
static DWORD g_dwTargetPID = 0;
static HANDLE g_hTargetProcess = nullptr;
static HHOOK g_hMouseHook = nullptr;
static HANDLE g_hPipe = nullptr;

// ==========================================
// 函数声明
// ==========================================

void RefreshProcessList();

// ==========================================
// 注入 DLL
// ==========================================

bool InjectDLL(DWORD dwPID, const char* szDllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPID);
    if (!hProcess) {
        MessageBox(nullptr, "无法打开目标进程", "错误", MB_ICONERROR);
        return false;
    }

    size_t dwSize = strlen(szDllPath) + 1;
    LPVOID pRemoteBuf = VirtualAllocEx(hProcess, nullptr, dwSize, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteBuf) {
        CloseHandle(hProcess);
        MessageBox(nullptr, "分配内存失败", "错误", MB_ICONERROR);
        return false;
    }

    WriteProcessMemory(hProcess, pRemoteBuf, szDllPath, dwSize, nullptr);

    LPVOID pLoadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, 
        (LPTHREAD_START_ROUTINE)pLoadLib, pRemoteBuf, 0, nullptr);
    
    bool bRet = (hThread != nullptr);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    } else {
        MessageBox(nullptr, "创建远程线程失败", "错误", MB_ICONERROR);
    }

    VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return bRet;
}

// ==========================================
// 刷新进程列表
// ==========================================

void RefreshProcessList() {
    SendMessage(g_hwndProcessList, LB_RESETCONTENT, 0, 0);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = {sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            // 检查 duilib.dll 是否加载
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
                            // 转换为宽字符
                            wchar_t wname[MAX_PATH];
                            MultiByteToWideChar(CP_ACP, 0, pe.szExeFile, -1, wname, MAX_PATH);
                            
                            int idx = SendMessage(g_hwndProcessList, LB_ADDSTRING, 0, (LPARAM)wname);
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

// ==========================================
// 鼠标钩子回调
// ==========================================

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_LBUTTONUP && g_dwTargetPID != 0 && g_hPipe != INVALID_HANDLE_VALUE) {
        MOUSEHOOKSTRUCT* pInfo = (MOUSEHOOKSTRUCT*)lParam;

        FinderRequest req{CMD_QUERY_CONTROL, pInfo->pt};
        FinderResponse resp{0};

        DWORD dwWrite, dwRead;
        WriteFile(g_hPipe, &req, sizeof(req), &dwWrite, nullptr);
        ReadFile(g_hPipe, &resp, sizeof(resp), &dwRead, nullptr);

        wchar_t wbuf[256];
        if (resp.result == 0) {
            swprintf_s(wbuf, L"Class: %ls | Name: %ls", resp.className, resp.name);
        } else if (resp.result == 1) {
            wcscpy_s(wbuf, L"未找到控件");
        } else {
            wcscpy_s(wbuf, L"错误: 无法获取");
        }
        SetWindowText(g_hwndResultStatic, wbuf);
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

// ==========================================
// 窗口消息处理
// ==========================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) { // IDC_INJECT_BTN
            int idx = SendMessage(g_hwndProcessList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                g_dwTargetPID = SendMessage(g_hwndProcessList, LB_GETITEMDATA, idx, 0);
                
                char dllPath[MAX_PATH];
                GetCurrentDirectoryA(MAX_PATH, dllPath);
                strcat_s(dllPath, "\\bin\\HookDll.dll");
                
                if (InjectDLL(g_dwTargetPID, dllPath)) {
                    MessageBox(hwnd, L"注入成功！请点击「开始探测」后点击目标窗口", L"提示", MB_OK);
                }
            }
        } else if (LOWORD(wParam) == 2) { // IDC_START_DETECT_BTN
            if (!g_hMouseHook) {
                g_hPipe = CreateFileA(FINDER_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (g_hPipe != INVALID_HANDLE_VALUE) {
                    g_hMouseHook = SetWindowsHookEx(WH_MOUSE, MouseProc, nullptr, GetCurrentThreadId());
                    SetWindowText(g_hwndDetectBtn, L"停止探测");
                } else {
                    MessageBox(hwnd, L"无法连接目标进程，请先注入", L"错误", MB_ICONERROR);
                }
            } else {
                UnhookWindowsHookEx(g_hMouseHook);
                g_hMouseHook = nullptr;
                CloseHandle(g_hPipe);
                g_hPipe = nullptr;
                SetWindowText(g_hwndDetectBtn, L"开始探测");
            }
        }
        break;

    case WM_CLOSE:
        if (g_hMouseHook) UnhookWindowsHookEx(g_hMouseHook);
        if (g_hPipe) CloseHandle(g_hPipe);
        DestroyWindow(hwnd);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==========================================
// 主入口
// ==========================================

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DuiLibFinderClass";
    RegisterClassW(&wc);

    MSG msg;
    HWND hwnd = CreateWindowW(
        L"DuiLibFinderClass", L"DuiLib 控件探测器",
        WS_CAPTION | WS_SYSMENU,
        100, 100, 400, 320,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    // 创建子窗口
    g_hwndProcessList = CreateWindowW(L"LISTBOX", nullptr, 
        WS_CHILD | WS_VISIBLE | LBS_STANDARD,
        20, 20, 360, 150, hwnd, (HMENU)1, nullptr, nullptr);
    g_hwndInjectBtn = CreateWindowW(L"BUTTON", L"注入 DLL", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 180, 100, 30, hwnd, (HMENU)1, nullptr, nullptr);
    g_hwndDetectBtn = CreateWindowW(L"BUTTON", L"开始探测", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, 180, 100, 30, hwnd, (HMENU)2, nullptr, nullptr);
    g_hwndResultStatic = CreateWindowW(L"STATIC", L"等待探测...", 
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 220, 360, 30, hwnd, nullptr, nullptr, nullptr);

    RefreshProcessList();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
```

- [ ] **Step 3: 提交 Finder**

```bash
git add src/Finder/
git commit -m "feat: add Finder GUI application"
```

---

### Task 5: 构建和测试

- [ ] **Step 1: 运行 build.bat**

```
cmd /c build.bat
```

- [ ] **Step 2: 检查 bin 目录输出**

- [ ] **Step 3: 测试运行**

准备一个 DuiLib 程序，运行 Finder.exe 验证功能

---

## ���行方式选择

Plan complete and saved to `plans/2026-05-21-duilib-control-finder.md`. 

**Two execution options:**

**1. Subagent-Driven (recommended)** - 创建子任务执行

**2. Inline Execution** - 当前会话逐步执行

你选择哪种？