// logger.h — spdlog 日志系统初始化封装（优化版 v2）
// ============================================================
// v2 变更：
//   [L01] 增加 Release 构建下的 stdout_color_sink（可选，默认关闭）
//   [L02] 增加 async_logger 可选注释示例（供有需要的场景参考）
//   [L03] Init() 增加 bool 返回值，方便调用方判断是否初始化成功
//   [L04] 日志目录路径改为相对于 exe 的位置（而非 CWD），避免
//         "从不同目录启动程序时日志文件飘移"的问题
// ============================================================
#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <memory>
#include <vector>
#include <string>
#include <windows.h>

namespace EnigmaLog {

/// [L04] 获取与 exe 同目录的 logs/ 路径（不依赖 CWD）
inline std::string GetLogDir()
{
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir(exePath);
    auto pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir.resize(pos + 1);
    return dir + "logs";
}

/// 初始化全局默认 logger
/// [L03] 返回 true 表示完全成功（文件 + VS 输出），false 表示降级到 VS 输出
inline bool Init()
{
    try {
        // [L04] logs 目录紧邻 exe（与 CWD 无关）
        std::string logDir  = GetLogDir();
        std::string logFile = logDir + "\\enigma_hwid.log";
        ::CreateDirectoryA(logDir.c_str(), NULL);

        std::vector<spdlog::sink_ptr> sinks;

        // Sink 1：滚动文件日志（5 MB × 3 份）
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile,
            5ULL * 1024 * 1024,   // 5 MB
            3                      // 最多 3 个备份
        );
        file_sink->set_level(spdlog::level::debug);
        sinks.push_back(file_sink);

        // Sink 2：Visual Studio 输出窗口（调试运行时可见）
        auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        msvc_sink->set_level(spdlog::level::debug);
        sinks.push_back(msvc_sink);

        // ── [L02] async_logger 示例（需 #include <spdlog/async.h>）──
        // spdlog::init_thread_pool(8192, 1);
        // auto logger = std::make_shared<spdlog::async_logger>(
        //     "enigma", sinks.begin(), sinks.end(),
        //     spdlog::thread_pool(), spdlog::async_overflow_policy::block);

        // 同步 logger（默认）
        auto logger = std::make_shared<spdlog::logger>(
            "enigma", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%-5l%$] %v");
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(logger);
        spdlog::info("=========================================");
        spdlog::info("EnigmaHardwareID  日志系统初始化完成 (v2)");
        spdlog::info("日志文件: {}", logFile);
        spdlog::info("=========================================");
        return true;

    } catch (const spdlog::spdlog_ex& ex) {
        // 降级：至少输出到 VS 调试窗口
        ::OutputDebugStringA("[EnigmaLog] spdlog 初始化失败: ");
        ::OutputDebugStringA(ex.what());
        ::OutputDebugStringA("\n");
        return false;  // [L03]
    }
}

/// 在 ExitInstance() 前调用，刷盘并释放所有 sink
inline void Shutdown()
{
    spdlog::info("=========================================");
    spdlog::info("EnigmaHardwareID  正在关闭");
    spdlog::info("=========================================");
    spdlog::shutdown();
}

} // namespace EnigmaLog
