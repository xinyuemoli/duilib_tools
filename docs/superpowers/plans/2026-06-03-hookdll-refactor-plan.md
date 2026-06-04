# HookDll 重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 HookDll 从单一文件重构为按职责分层的模块化结构

**Architecture:** 
- `common/` - 通用工具（线程枚举）
- `duilib/` - duilib.dll 交互（模块查找、函数查找）
- `hooks/` - 具体 Hook 实现（MessageHandler）
- `HookDll.cpp` - DllMain 入口，保持最小

**Tech Stack:** C++, Windows API, detours.lib

---

## 文件映射

创建以下文件：

| 操作 | 路径 | 职责 |
|------|------|------|
| 新建 | `common/ThreadEnum.h` | 线程枚举头文件 |
| 新建 | `common/ThreadEnum.cpp` | 线程枚举实现 |
| 新建 | `duilib/ModuleFinder.h` | 模块查找头文件 |
| 新建 | `duilib/ModuleFinder.cpp` | 模块查找实现 |
| 新建 | `hooks/MessageHandler.h` | Hook 头文件 |
| 新建 | `hooks/MessageHandler.cpp` | Hook 实现 |
| 修改 | `HookDll.cpp` | 简化为入口，调用各模块 |
| 修改 | `CMakeLists.txt` | 添加新源文件 |

---

### Task 1: 创建 common/ThreadEnum 模块

**Files:**
- Create: `src/HookDll/common/ThreadEnum.h`
- Create: `src/HookDll/common/ThreadEnum.cpp`

- [ ] **Step 1: 创建 ThreadEnum.h**

```cpp
#pragma once
#include <windows.h>
#include <vector>

// 获取进程内除当前线程外的所有线程句柄
std::vector<HANDLE> GetAllOtherThreadsInProcess();
```

- [ ] **Step 2: 创建 ThreadEnum.cpp**

将原 HookDll.cpp 中的 `GetAllOtherThreadsInProcess()` 函数移到此处。

```cpp
#include "ThreadEnum.h"
#include <tlhelp32.h>

std::vector<HANDLE> GetAllOtherThreadsInProcess() {
    std::vector<HANDLE> otherThreads;
    // ... 原实现
}
```

- [ ] **Step 3: Commit**

```bash
git add src/HookDll/common/
git commit -m "refactor: 添加 common/ThreadEnum 模块"
```

---

### Task 2: 创建 duilib/ModuleFinder 模块

**Files:**
- Create: `src/HookDll/duilib/ModuleFinder.h`
- Create: `src/HookDll/duilib/ModuleFinder.cpp`

- [ ] **Step 1: 创建 ModuleFinder.h**

```cpp
#pragma once
#include <windows.h>

// 获取 duilib.dll 模块句柄
HMODULE GetDuiLibModule();

// 从 duilib.dll 查找导出函数
PVOID FindDuiLibExport(const char* funcName);
```

- [ ] **Step 2: 创建 ModuleFinder.cpp**

将原 HookDll.cpp 中的 `GetDuilibModule()` 和 `FindImportFunctions()` 整合至此。

```cpp
#include "ModuleFinder.h"
#include "../common/ThreadEnum.h"

static HMODULE g_hDuiLib = nullptr;

HMODULE GetDuiLibModule() {
    if (!g_hDuiLib) {
        g_hDuiLib = GetModuleHandleA("duilib.dll");
    }
    return g_hDuiLib;
}

PVOID FindDuiLibExport(const char* funcName) {
    if (!g_hDuiLib) return nullptr;
    return GetProcAddress(g_hDuiLib, funcName);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/HookDll/duilib/
git commit -m "refactor: 添加 duilib/ModuleFinder 模块"
```

---

### Task 3: 创建 hooks/MessageHandler 模块

**Files:**
- Create: `src/HookDll/hooks/MessageHandler.h`
- Create: `src/HookDll/hooks/MessageHandler.cpp`

- [ ] **Step 1: 创建 MessageHandler.h**

```cpp
#pragma once
#include <windows.h>

// 安装 MessageHandler Hook
BOOL InstallMessageHandlerHook();

// 卸载 MessageHandler Hook
BOOL UninstallMessageHandlerHook();
```

- [ ] **Step 2: 创建 MessageHandler.cpp**

将原 HookDll.cpp 中的 `CPaintManagerUI_Hook` 类和 Hook 安装/卸载逻辑移至此。

```cpp
#include "MessageHandler.h"
#include "../duilib/ModuleFinder.h"
#include "../common/ThreadEnum.h"
#include "detours/detours.h"

// 类型定义（从原代码保留）
class CPaintManagerUI_Hook {
public:
    BOOL DetouredMessageHandler(UINT msg, UINT wParam, INT lParam, INT* pResult);
};

typedef BOOL(CPaintManagerUI_Hook::* FN_MessageHandler)(UINT msg, UINT wParam, INT lParam, INT* pResult);

// 静态成员（从原代码保留）
static FN_MessageHandler g_originPainterMessageHandle = nullptr;
static PVOID g_pRawOriginMessageHandler = nullptr;
static BOOL g_bHookInstalled = FALSE;

// 实现...
```

- [ ] **Step 3: Commit**

```bash
git add src/HookDll/hooks/
git commit -m "refactor: 添加 hooks/MessageHandler 模块"
```

---

### Task 4: 简化 HookDll.cpp 为入口

**Files:**
- Modify: `src/HookDll/HookDll.cpp`

- [ ] **Step 1: 重写 HookDll.cpp**

简化为 DllMain 入口，只负责调度。

```cpp
#include <windows.h>
#include "hooks/MessageHandler.h"

DWORD WINAPI InitHookThread(LPVOID) {
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
```

- [ ] **Step 2: Commit**

```bash
git add src/HookDll/HookDll.cpp
git commit -m "refactor: 简化 HookDll.cpp 为入口"
```

---

### Task 5: 更新 CMakeLists.txt

**Files:**
- Modify: `src/HookDll/CMakeLists.txt`

- [ ] **Step 1: 更新 CMakeLists.txt**

```cmake
add_library(HookDll SHARED
    HookDll.cpp
    HookDll.def
    common/ThreadEnum.cpp
    duilib/ModuleFinder.cpp
    hooks/MessageHandler.cpp
)
```

- [ ] **Step 2: Commit**

```bash
git add src/HookDll/CMakeLists.txt
git commit -m "build: 更新 CMakeLists.txt 添加新模块"
```

---

### Task 6: 测试编译

**Files:**
- Build: `src/HookDll/`

- [ ] **Step 1: 运行构建脚本**

```bash
cd f:/codes/duilib_tools
./build.bat
```

- [ ] **Step 2: 检查 DLL 是否生成**

确认 `bin/Release/HookDll.dll` 生成成功。

- [ ] **Step 3: Commit**

```bash
git add bin/Release/HookDll.dll
git commit -m "build: HookDll 重构完成"
```

---

## 验收标准

- [ ] 代码按职责分层：common/、duilib/、hooks/
- [ ] HookDll.cpp 简化为入口（< 50 行）
- [ ] 编译通过，DLL 正常生成
- [ ] 原有 Hook 功能正常工作