// EnigmaHardwareID.cpp — 应用程序行为定义（现代化版，VS2017+）
// ============================================================
// 变更：[M] 添加 spdlog 初始化 / 关闭
// ============================================================
#include "stdafx.h"
#include "EnigmaHardwareID.h"
#include "EnigmaHardwareIDDlg.h"
#include "logger.h"

#ifdef _DEBUG
#  define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// CEnigmaHardwareIDApp 消息映射

BEGIN_MESSAGE_MAP(CEnigmaHardwareIDApp, CWinApp)
    ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CEnigmaHardwareIDApp 构造

CEnigmaHardwareIDApp::CEnigmaHardwareIDApp()
{
    // 重要初始化请放在 InitInstance() 中
}

/////////////////////////////////////////////////////////////////////////////
// 唯一的应用程序对象

CEnigmaHardwareIDApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CEnigmaHardwareIDApp 初始化

BOOL CEnigmaHardwareIDApp::InitInstance()
{
    // ── [M] 初始化 spdlog 日志系统 ─────────────────────────
    EnigmaLog::Init();
    // ────────────────────────────────────────────────────────

    CEnigmaHardwareIDDlg dlg;
    m_pMainWnd = &dlg;
    int nResponse = dlg.DoModal();

    if (nResponse == IDOK) {
        spdlog::debug("对话框以 IDOK 关闭");
    } else if (nResponse == IDCANCEL) {
        spdlog::debug("对话框以 IDCANCEL 关闭");
    }

    // ── [M] 关闭 spdlog（刷盘并释放所有 sink）──────────────
    EnigmaLog::Shutdown();
    // ────────────────────────────────────────────────────────

    // 对话框已关闭，返回 FALSE 以退出程序（不启动消息泵）
    return FALSE;
}
