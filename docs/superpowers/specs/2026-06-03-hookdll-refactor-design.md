# HookDll 重构设计

## 概述

将 HookDll 从单一文件重构为按职责分层的模块化结构，支持未来扩展更多 Hook 点。

## 目标目录结构

```
src/HookDll/
├── CMakeLists.txt
├── HookDll.cpp              # DllMain 入口
├── HookDll.def              # 导出定义
│
├── duilib/                  # duilib 交互层
│   ├── ModuleFinder.h       # 查找 duilib 模块
│   └── ModuleFinder.cpp
│
├── hooks/                   # 具体 Hook 实现
│   ├── MessageHandler.h     # CPaintManagerUI::MessageHandler Hook
│   └── MessageHandler.cpp
│
└── common/                 # 通用工具
    ├── ThreadEnum.h         # 线程枚举
    └── ThreadEnum.cpp
```

## 模块职责

| 目录 | 职责 | API |
|------|------|-----|
| `duilib/` | 加载 duilib.dll，查找导出函数 | `GetDuiLibModule()`, `FindExport()` |
| `hooks/` | 具体 Hook 的拦截逻辑 | `InstallHook()`, `UninstallHook()` |
| `common/` | 所有 Hook 共用工具 | `GetAllOtherThreadsInProcess()` |

## 模块依赖

```
DllMain
   ↓
duilib::FindExport()      
   ↓
common::SuspendOtherThreads()  
   ↓
hooks::MessageHandler::Install()  
```

## 设计原则

1. **单一职责**：每个模块只做一件事
2. **依赖单向**：依赖方向是从入口到具体实现，不循环
3. **易于扩展**：新增 Hook 只需在 hooks/ 目录下添加新文件
4. **隐藏细节**：各模块通过头文件暴露最小接口

## 迁移清单

- [ ] 创建 `common/` 目录，移动线程枚举代码
- [ ] 创建 `duilib/` 目录，移动模块查找代码
- [ ] 创建 `hooks/` 目录，移动 Hook 实现代码
- [ ] 更新 `CMakeLists.txt` 添加新的源文件
- [ ] 测试编译和运行