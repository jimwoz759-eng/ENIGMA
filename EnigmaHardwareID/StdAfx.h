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

// Header-only 引入（vcpkg 或手动放置 include/spdlog/）
#include <spdlog/spdlog.h>
// ────────────────────────────────────────────────────────────
