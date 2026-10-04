// stdafx.h — 预编译头文件（现代化版，VS2017+）
// ============================================================
// 变更：
//   [M] 移除 VC6 专用 #if _MSC_VER > 1000 / #pragma once 冗余写法
//   [M] 添加 spdlog 全局宏配置与 include
// ============================================================
#pragma once

#define VC_EXTRALEAN    // 排除 Windows 头文件中较少使用的组件

#include <afxwin.h>     // MFC 核心与标准组件
#include <afxext.h>     // MFC 扩展
#include <afxdtctl.h>   // IE4 公共控件支持
#ifndef _AFX_NO_AFXCMN_SUPPORT
#  include <afxcmn.h>   // Windows 公共控件支持
#endif

// ── spdlog 配置 ─────────────────────────────────────────────
// SPDLOG_ACTIVE_LEVEL 决定编译时过滤的最低日志级别：
//   Debug 构建   → debug 及以上全部输出
//   Release 构建 → info  及以上输出（debug 语句被编译器优化掉）
#ifndef SPDLOG_ACTIVE_LEVEL
#  ifdef _DEBUG
#    define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#  else
#    define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#  endif
#endif

// ── fmt header-only 模式 ─────────────────────────────────────
// 强制 fmt 以 header-only 方式编译进 .exe，彻底消除对 fmt.dll 的运行时依赖。
// 必须在任何 spdlog / fmt 头文件之前定义。
// 原理：
//   FMT_HEADER_ONLY        → fmt 的所有实现都内联在头文件里，不链接 fmt.lib/fmt.dll
//   SPDLOG_FMT_EXTERNAL    → 告知 spdlog 使用外部 fmt（即上面那份 header-only fmt）
//                            而非 spdlog 自带的 bundled fmt，两者路径一致即可
#ifndef FMT_HEADER_ONLY
#  define FMT_HEADER_ONLY
#endif
#ifndef SPDLOG_FMT_EXTERNAL
#  define SPDLOG_FMT_EXTERNAL
#endif
// ────────────────────────────────────────────────────────────

// Header-only 引入（vcpkg 或手动放置 include/spdlog/）
#include <spdlog/spdlog.h>
// ────────────────────────────────────────────────────────────
