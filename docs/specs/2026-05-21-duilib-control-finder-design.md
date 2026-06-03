# DuiLib 控件探测器 - 设计规格

**日期**: 2026-05-21
**状态**: 待用户审批

## 1. 概述

开发一个类似 Spy++ 的工具，用于在运行时检测 DuiLib 界面上鼠标点击所指向的控件，并输出其类名和控件名称。

### 核心功能
- 通过 IPC 请求-响应模式（非 Hook）
- 点击目标程序窗口时自动识别控件信息
- 输出：DuiLib 类名（如 CButtonUI）+ Name 属性

### 目标场景
- 逆向分析 DuiLib 编写的桌面程序
- 辅助调试和界面检查

---

## 2. 技术方案

### 2.1 整体架构

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   Finder.exe    │◀───▶│  命名管道      │◀───▶│ HookDll.dll    │
│ (界面+客户端)   │     │ (IPC通信)       │     │ (服务端)      │
└─────────────────┘     └──────────────────┘     └─────────────────┘
         │                                                 ▲
         │                    注入                           │
         └─────────────────────────────────────────────────┘
```

### 2.2 通信协议

#### 命名管道配置
- **管道名**: `\\.\pipe\DuiLibFinderPipe`
- **模式**: 双向通信
- **服务端**: HookDll.dll
- **客户端**: Finder.exe

#### 消息格式

**请求 (exe → dll)**:
```
struct FinderRequest {
    DWORD   cmd;        // 1 = 查询坐标所在控件
    POINT   pt;         // 屏幕坐标
};
```

**响应 (dll → exe)**:
```
struct FinderResponse {
    DWORD   result;     // 0 = 成功, 1 = 未找到, 2 = 错误
    wchar_t className[64];   // 控件类名
    wchar_t name[64];        // 控件名称
};
```

### 2.3 关键技术点

#### 2.3.1 获取 CPaintManagerUI* 指针
- **函数**: `MessageHandler` 
- **地址**: 导出序数 2341，`?MessageHandler@CPaintManagerUI@DuiLib@@QAE_NIIJAAJ@Z`
- **原理**：Hook 此函数，目标程序调用时会传递 this 指针（ECX），从中获取 `CPaintManagerUI*`

#### 2.3.2 控件查找
- **函数**: `CPaintManagerUI::FindControl(POINT pt)`
- **地址**: `?FindControl@CPaintManagerUI@DuiLib@@QBEPAVCControlUI@2@UtagPOINT@@@Z`
- **签名**: `CControlUI* __thiscall CPaintManagerUI::FindControl(POINT)`

#### 2.3.3 信息提取
- **GetClass()**: 导出序数 1262，`?GetClass@CControlUI@DuiLib@@UBEPB_WXZ`
- **GetName()**: 导出序数 1694，`?GetName@CControlUI@DuiLib@@UBE?AVCDuiString@2@XZ`

### 2.4 注入方式
- 使用远程线程 + `LoadLibrary` 的经典 DLL 注入方式

---

## 3. 模块设计

### 3.1 Finder.exe

桌面程序，负责：

```
职责:
- 显示进程列表，供用户选择目标进程
- 注入 HookDll.dll 到目标进程
- 监听鼠标拖动（全局鼠标钩子）
- 鼠标弹起时发送请求到命名管道
- 显示查询结果
```

### 3.2 HookDll.dll

注入到目标进程的 DLL，负责：

```
职责:
- 启动命名管道服务器
- 等待接收查询请求
- 调用 CPaintManagerUI::FindControl()
- 返回控件信息
```

---

## 4. 工作流程

```
1. 启动 Finder.exe
2. 用户选择目标进程（从进程列表中选择或窗口拖拽）
3. 点击"注入"按钮
4. Injector 将 HookDll.dll 注入目标进程
5. HookDll 创建命名管道并监听
6. 用户开启"探测模式"
7. Finder 安装全局鼠标钩子
8. 用户在目标窗口拖动鼠标
9. 鼠标弹起时，Finder 发送坐标到命名管道请求
10. HookDll 接收到请求，查找控件
11. HookDll 返回类名+名称到 Finder
12. Finder 显示结果
```

---

## 5. 编译环境

| 项目 | 要求 |
|------|------|
| 语言 | C++ (MSVC) |
| 架构 | 32-bit |
| 依赖 | 仅 Windows SDK |
| 输出 | Finder.exe + HookDll.dll |

---

## 6. 接口定义

### 5.1 DuiLib 导出函数（从二进制分析）

```cpp
// 这些是导出的 C++ 成员函数，第一个参数是 this 指针

// 按坐标查找控件
// 地址：导出序数对应 ?FindControl@CPaintManagerUI@DuiLib@@QBEPAVCControlUI@2@UtagPOINT@@@Z
typedef CControlUI* (__thiscall* PFN_FindControl)(CPaintManagerUI* this, POINT pt);

// 获取控件类名
// 地址：导出序数 1262
typedef const wchar_t* (__thiscall* PFN_GetClass)(CControlUI* this);

// 获取控件名称 (Name 属性)
// 地址：导出序数 1694
typedef CDuiString (__thiscall* PFN_GetName)(CControlUI* this);
```

---

## 7. 验收标准

- [ ] 能够列出系统中的所有进程
- [ ] 成功注入到目标进程
- [ ] 点击按钮区域，正确显示 "ButtonUI" + Name
- [ ] 点击编辑框，正确显示 "EditUI" + Name
- [ ] 点击空白区域，显示未找到
- [ ] 输出格式：`Class: xxx | Name: yyy`