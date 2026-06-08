# DuiLib Tools

一个针对 DuiLib 界面的运行时分析与辅助工具集，通过 DLL 注入、API Hook 和 COM/MSAA 技术，在无源码条件下对第三方 DuiLib 应用程序进行二进制级扩展。

---

## 项目组成

| 组件 | 类型 | 职责 |
|------|------|------|
| `Finder.exe` | GUI 应用程序 | 进程枚举、DLL 注入器、Spy++ 风格鼠标捕获 |
| `HookDll.dll` | 共享库（被注入） | 目标进程内运行，提供 MSAA/IAccessible 支持与 IPC 服务端 |

---

## 技术原理

### 1. DLL 注入（CreateRemoteThread + LoadLibrary）

Finder 通过经典的远程线程注入将 `HookDll.dll` 载入目标进程：

1. `OpenProcess` 获取目标进程句柄
2. `VirtualAllocEx` 在目标进程分配内存
3. `WriteProcessMemory` 写入 DLL 路径字符串
4. `CreateRemoteThread` 执行 `LoadLibraryA`，加载 HookDll

这是 Windows 平台上最基础的 DLL 注入方式，无需驱动或签名，但依赖目标进程的 `PROCESS_ALL_ACCESS` 权限。

### 2. API Hook（Microsoft Detours）

HookDll 使用 **Microsoft Detours** 库拦截 `CPaintManagerUI::MessageHandler`：

```
原始调用链:
  目标程序窗口消息 -> CPaintManagerUI::MessageHandler

Hook 后调用链:
  目标程序窗口消息 -> DetouredMessageHandler (HookDll)
                              -> 处理 WM_GETOBJECT 等自定义逻辑
                              -> 调用原始 MessageHandler
```

Detours 通过重写目标函数的前几条指令为跳转指令（trampoline），将控制流重定向到 Hook 函数。相比 Inline Hook 的手动字节操作，Detours 在多线程环境下更安全，因为它会在事务中挂起所有相关线程，原子性地完成指令替换。

### 3. 逆向工程与 vtable 调用

DuiLib 是 C++ 类库，没有提供 C 接口导出。HookDll 在无源码、无头文件的条件下操作 DuiLib 对象，核心技术是 **vtable（虚函数表）直接调用**：

- 通过 IDA Pro 分析 `CControlUI` 的 vtable 布局，确定各虚函数的索引：
  - `[5]` GetName
  - `[7]` GetClass
  - `[8]` GetInterface
  - `[17]` GetParent
  - `[22]` GetPos
- 运行时读取对象头 4 字节（32-bit）获取 vtable 指针
- 按索引取出函数指针，以 `__thiscall` 调用约定直接调用

同时通过 `GetProcAddress` 解析非虚函数的修饰名（mangled name）导出，如：
- `?GetData@CDuiString@DuiLib@@QBEPB_WXZ` —— CDuiString::GetData
- `?GetRoot@CPaintManagerUI@DuiLib@@QBEPAVCControlUI@2@XZ` —— CPaintManagerUI::GetRoot

### 4. SEH（结构化异常处理）稳定性保障

由于操作的是第三方进程的未公开对象，所有 vtable 调用都包装在 `__try/__except` 中：

- 目标控件可能已被销毁（悬空指针）
- vtable 指针可能被堆内存重用覆盖
- 虚函数调用可能触发访问违例（AV）

SEH 捕获这些异常后返回安全默认值，避免注入 DLL 的崩溃导致目标进程崩溃。

### 5. MSAA / IAccessible 实现

HookDll 拦截窗口的 `WM_GETOBJECT` 消息，返回自定义的 `IAccessible` 实现（`MyAccessibleImpl`）：

- 将 DuiLib 控件树映射到 MSAA 层次结构
- 实现 `accNavigate` 支持父子、前后兄弟遍历
- 实现 `get_accName`、`get_accRole`、`get_accChild` 等接口
- 使屏幕阅读器（如 NVDA、JAWS）能够识别 DuiLib 界面元素

这是无源码条件下为闭源 UI 框架添加无障碍支持的标准做法。

### 6. 动态缓存清理

`g_accessibleMap` 缓存了控件指针到 `IAccessible` wrapper 的映射。由于控件生命周期不受 HookDll 控制，已销毁控件的指针会变成悬空指针。

HookDll 实现了多层次的动态清理：

1. **存活检测（SEH + vtable）**：尝试安全调用 `GetClass` 虚函数，若触发异常则判定控件已销毁
2. **后台清理线程**：每 3 秒扫描缓存，自动释放已销毁控件对应的 wrapper
3. **阈值触发**：缓存达到 100 条时立即触发清理，防止内存无界增长
4. **卸载清理**：DLL 卸载时停止线程并释放全部剩余缓存

---

## 项目特点

1. **纯二进制级操作**：无需 DuiLib 源码、头文件或 PDB，完全基于逆向分析结果工作
2. **技术栈紧凑**：除 Detours 外无其他第三方依赖，GUI 使用原始 Win32 API
3. **多技术融合**：将 DLL 注入、API Hook、逆向工程、COM/MSAA、SEH 整合在一个项目中
4. **32-bit 深度适配**：针对 MSVC `__thiscall`、32-bit vtable 布局、对象内存模型做了精确适配
5. **稳定性设计**：所有危险操作均有 SEH 保护，缓存有自动清理机制

---

## 劣势与局限

1. **架构绑定（32-bit only）**
   - 硬编码了 `__thiscall` 调用约定、4 字节指针、vtable 索引
   - 64-bit 下调用约定（`__fastcall` 变体）和对象布局完全不同，无法直接迁移

2. **版本脆弱性**
   - vtable 索引、类名字符串匹配（如 `L"Button"`、`L"Edit"`）高度依赖 DuiLib 特定版本
   - DuiLib 重新编译后虚函数顺序可能变化，导致 vtable 调用错位

3. **功能不完整**
   - HookDll 中缺少命名管道服务端（`PipeServerThread`）的完整实现
   - Finder 的"Start Detect"功能（基于 `CMD_QUERY_CONTROL` 的坐标查询）目前无法正常工作
   - 计划中的 `CPaintManagerUI::FindControl(POINT)` 调用未实现

4. **注入的固有缺陷**
   - 需要 `PROCESS_ALL_ACCESS`，对高完整性级别进程（如管理员权限程序）注入会失败
   - 极易被杀毒软件/EDR 标记为恶意行为
   - 目标进程崩溃时，注入 DLL 的堆栈可能出现在崩溃转储中，增加调试难度

5. **代码维护性**
   - 大量使用原始 Win32 API 和 C 风格字符串操作，代码冗长
   - 全局状态（`g_accessibleMap`、函数指针）集中管理，缺乏模块化边界
   - 缺少单元测试和自动化验证手段

6. **无障碍支持的局限**
   - IAccessible 实现是简化版，缺少焦点追踪、选择状态、键盘快捷键等高级属性
   - 仅支持基础导航（父子、前后兄弟），不支持表格、树形等复杂结构的完整语义

---

## 参考意义

### 对逆向工程的参考价值

- **vtable 调用的实战模板**：展示了如何在无头文件条件下，通过对象头指针和索引调用 C++ 虚函数
- **IDA Pro 分析结果的工程化落地**：将 IDA 中的 vtable 分析转化为可直接运行的 `__thiscall` 函数指针调用

### 对 Windows 系统编程的参考价值

- **Detours 的标准使用范式**：事务式 Hook（`DetourTransactionBegin` / `UpdateThread` / `Attach` / `Commit`）
- **CreateRemoteThread 注入的完整流程**：从进程枚举、内存分配到远程线程执行的端到端示例
- **SEH 在注入场景下的最佳实践**：将 SEH 作为"沙箱"使用，而非仅作为错误报告机制

### 对无障碍开发的参考价值

- **为闭源 UI 框架添加 MSAA 支持的路径**：当目标程序没有实现 `IAccessible` 时，通过注入实现外部 shim
- **IAccessible 最小实现参考**：展示了 `IUnknown` + `IDispatch` + `IAccessible` 的核心接口实现方式

### 对内存安全设计的参考价值

- **外部对象生命周期管理**：通过 SEH 探测 + 定期后台扫描，解决"引用已释放的外部对象"这一经典难题
- **COM 对象缓存的自动回收**：结合引用计数（`AddRef/Release`）和外部存活检测的双保险设计

---

## 构建

要求：Windows + Visual Studio 2022 + CMake 3.15+

```bat
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A Win32 ..
cmake --build . --config Release
```

输出：
- `bin/Release/Finder.exe`
- `bin/Release/HookDll.dll`

---

## 运行方式

1. 启动 `Finder.exe`
2. 从列表中选择加载了 `duilib.dll` 的目标进程
3. 点击 **Inject DLL** 进行注入
4. 当前版本下，Finder 的控件坐标查询功能需要 HookDll 端的管道服务端实现配合方可工作
