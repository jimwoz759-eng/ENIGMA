// EnigmaHardwareID.h — 应用程序主头文件（现代化版，VS2017+）
#pragma once

#ifndef __AFXWIN_H__
#  error "请在包含此文件之前包含 'stdafx.h'（以生成 PCH）"
#endif

#include "resource.h"

// CEnigmaHardwareIDApp — 应用程序类
class CEnigmaHardwareIDApp : public CWinApp
{
public:
    CEnigmaHardwareIDApp();

    virtual BOOL InitInstance();

    DECLARE_MESSAGE_MAP()
};
