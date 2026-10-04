# EnigmaHardwareID — VC6 → VS2017+ 迁移指南（v2 优化版）

> **原始版本**：Visual C++ 6.0（1998）  
> **v1 重构**  ：VS2017 现代化（M01–M09 + spdlog）  
> **v2 优化**  ：性能 / 安全 / 可维护性全面提升

---

## 一、快速开始（5 步完成）

### 步骤 1：安装 vcpkg 并集成到 Visual Studio

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg integrate install   # 全局集成（只需执行一次）
```

### 步骤 2：安装 spdlog（x86-windows）

```powershell
# 项目使用 vcpkg.json 清单模式，VS 打开解决方案后会自动安装依赖
C:\vcpkg\vcpkg install --triplet x86-windows
```

### 步骤 3：用 Visual Studio 打开解决方案

```
双击：EnigmaHardwareID.sln
```

### 步骤 4：选择配置并生成

- 工具栏选 `Debug | Win32` 或 `Release | Win32`  
- `Ctrl+Shift+B` → 生成解决方案  
- 输出文件：`bin\Debug\EnigmaHardwareID.exe`

### 步骤 5：运行程序

```
bin\Debug\EnigmaHardwareID.exe
```

日志文件自动生成在 **exe 所在目录**的 `logs\enigma_hwid.log`（v2 新增：不再依赖 CWD）。

---

## 二、完整变更清单

### v1 变更（VC6 → VS2017 现代化）

| 编号 | 类别       | 原始（VC6）                      | v1 现代化（VS2017+）                  |
|------|-----------|----------------------------------|---------------------------------------|
| M01  | 类型定义   | 手动 `typedef int8_t` 等         | `#include <cstdint>`                  |
| M02  | 内存管理   | `std::auto_ptr<>`                | `std::unique_ptr<>`                   |
| M03  | 循环作用域 | `for` 变量泄漏到外层              | 标准 C++11 限定域                      |
| M04  | 字符串操作 | `strcat()` 裸字符数组            | `std::string` 拼接                    |
| M05  | **日志**   | `printf()` / 无日志              | **spdlog 结构化日志**                  |
| M06  | 格式化     | `sprintf`、`strcpy`              | `sprintf_s`、`strcpy_s`              |
| M07  | 数组初始化 | `for` 逐字节清零                  | `memset()`                            |
| M08  | 堆内存     | `malloc` + 裸指针                | `std::unique_ptr<BYTE[]>`             |
| M09  | 重复逻辑   | 函数内重复代码块                   | C++11 Lambda                          |
| ASM  | 内联汇编   | `_asm { ... }`                   | **保持不变**（VS2017 x86 支持）        |

### v2 变更（优化版）

| 编号 | 类别           | 问题描述                                              | v2 修复 / 改进                            |
|------|---------------|------------------------------------------------------|------------------------------------------|
| O01  | 全局变量消除   | 6 个全局字符数组（HDDStr 等）污染命名空间、多实例不安全  | 全部迁移为 `CEnigmaHardwareIDDlg` 私有成员 |
| O02  | RC4 S-Box 类型 | `int m_sBox[256]`（1024 字节）                        | `std::array<uint8_t, 256>`（256 字节）    |
| O03  | 冗余成员变量   | `a`/`b`/`swap` 跨函数共享状态                          | 删除，改为 `InitializeRC4Key` 局部变量     |
| O04  | 重复 UI 代码   | `OnGetbutton` 中 8 处几乎相同的 SetDlgItemText+CRC 块  | `SetComponentCRC` / `ClearComponent` 辅助 |
| O05  | 硬件采集返回值 | 用空字符串检测成功与否（语义不清）                      | 改为 `bool` 返回值                        |
| O06  | CRC 表重建     | 每次点击都在栈上重建 256×4 字节表                      | 静态成员 + 懒初始化（只建一次）            |
| O07  | 注册表 Bug     | `RegOpenKeyExA` 失败时仍调用 `RegCloseKey(未初始化句柄)` | 提前返回，不关闭无效句柄                  |
| O08  | RC4 栈帧       | `RC4()` 内拷贝 `int sBox[256]`（1024 字节栈）          | 改为 `std::array<uint8_t,256>`（256 字节）|
| O09  | `hextobyte`    | 普通函数（运行时求值）                                  | `constexpr`（编译期常量折叠）             |
| O10  | HDD 栈帧       | `char buffer[10000]` 在栈上（10 KB）                   | `std::vector<char>`（堆分配，大小自适应） |
| O11  | KeyLen 越界    | RSA Key 中的 KeyLen 未做上界检查                       | 增加 `KeyLen < MAX_PATH - 3` 校验         |
| O12  | RC4 密钥重复   | `OnDecodehwid` / `OnGenhwid` 各有一份 S-Box 加载逻辑   | 提取为 `LoadSBoxFromEdit()` 静态辅助函数  |
| O13  | 分隔符位置错误 | `EncodeToString` 的 `insertCount` 与位置条件不同步     | 改为独立 `outPos` + `nibbleCount` 计数    |
| L01–L04 | 日志增强  | 日志目录依赖 CWD；无 async 示例                        | 日志目录改为 exe 同级；增加 async 示例注释|

---

## 三、已知限制

| 项目                  | 状态                                                                 |
|-----------------------|----------------------------------------------------------------------|
| x86 内联汇编（CPUID） | ✅ 正常（VS2017 x86 完整支持）                                         |
| x64 构建              | ❌ `_asm` 块在 x64 不支持；**修复路径**：改用 `__cpuid()` / `__cpuidex()` 内置函数 |
| Unicode 字符集        | ⚠️ 代码使用 MBCS；切换 Unicode 需修改字符串字面量和 API 后缀          |
| 异步 spdlog           | 可选：参见 `logger.h` 中 `[L02]` 注释示例                            |
| 多实例安全            | ✅ v2 已通过成员变量消除全局状态，多实例场景更安全                    |

---

## 四、文件结构

```
EnigmaHardwareID-VS2017/
├── EnigmaHardwareID.sln
├── UPGRADE_GUIDE.md                      ← 本文件（v2）
└── EnigmaHardwareID/
    ├── EnigmaHardwareID.vcxproj
    ├── EnigmaHardwareID.vcxproj.filters
    ├── vcpkg.json
    ├── logger.h                          ← spdlog 封装（v2：exe 同级日志目录）
    ├── config.h                          ← RSA Demo 密钥（不变）
    ├── StdAfx.h / StdAfx.cpp
    ├── EnigmaHardwareID.h / .cpp         ← 应用入口
    ├── EnigmaHardwareIDDlg.h             ← v2：成员变量化、uint8_t S-Box
    ├── EnigmaHardwareIDDlg.cpp           ← v2：O01–O13 全部优化
    ├── resource.h / EnigmaHardwareID.rc
    └── res/
```

---

## 五、日志文件（v2）

**位置**：exe 所在目录下的 `logs\enigma_hwid.log`（不再依赖启动时的当前目录）

**轮转规则**：单文件 5 MB，最多保留 3 份备份

**格式**：
```
[2026-10-04 09:15:22.123] [info ] EnigmaHardwareID 日志系统初始化完成 (v2)
[2026-10-04 09:15:22.124] [info ] 日志文件: C:\...\logs\enigma_hwid.log
[2026-10-04 09:15:22.250] [info ] === 开始采集硬件指纹 ===
[2026-10-04 09:15:22.310] [debug] Component[0] CRC32=AABBCCDD
```

**Release 模式**：`debug` 语句被编译器优化去除（零开销），仅保留 `info` 及以上。
