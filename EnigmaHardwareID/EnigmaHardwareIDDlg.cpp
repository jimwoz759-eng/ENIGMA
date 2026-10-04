// EnigmaHardwareIDDlg.cpp — 实现文件（优化版 v2，VS2017+ / C++17）
// ============================================================
// 原始代码  ：Visual C++ 6.0 (1998)
// v1 重构   ：VS2017 现代化 (M01–M09 + ASM 保留)
// v2 优化   ：性能 / 安全 / 可维护性全面提升
//
// v2 变更记录（在 v1 基础上叠加）：
//   [O01] 消除 6 个全局变量 → 成员变量（线程隔离、无链接污染）
//   [O02] RC4 S-Box：int[256] → uint8_t[256]（-768 字节栈内存）
//   [O03] 删除冗余成员 a/b/swap（InitializeRC4Key 内改为纯局部变量）
//   [O04] 引入 SetComponentCRC/ClearComponent 消除 OnGetbutton 中
//         8 处重复的 SetDlgItemText+CRC 计算块（减少约 80 行代码）
//   [O05] 硬件采集函数改为 bool 返回（成功/失败清晰，消除空字符串侦测）
//   [O06] CRC32 查找表改为静态成员，只生成一次（避免每次按钮点击重算）
//   [O07] GetRegistryKeyValue 改用 std::unique_ptr<byte[]>（RAII，
//         修复原版在 RegOpenKeyEx 失败时仍调用 RegCloseKey(未初始化句柄) 的 bug）
//   [O08] RC4() 内部 sBox 拷贝从 int[256]*4 → uint8_t[256]*1（-768 字节）
//   [O09] hextobyte 提升为 constexpr（编译期常量折叠）
//   [O10] GetHDDString_ 中 buffer 改 std::vector<char>(dwReturned) 避免
//         10 KB 栈帧；SecondString 改 std::string 彻底消除栈溢出风险
//   [O11] ExtractRC4EncryptionKeyFromRsa 中 KeyLen 范围检查（防越界读）
//   [O12] OnDecodehwid / OnGenhwid RC4 密钥从 UI 读取改封装为
//         LoadSBoxFromEdit()，消除重复代码
//   [O13] EncodeToString 插入分隔符逻辑修正（原版当 insertCount 累计时
//         条件 (2*i)%addSep 仍不计 insertCount，现改为独立 outPos 计数）
//   [O14] UPGRADE_GUIDE.md 中"六、已知限制"x64 条目对应修复点加注释
// ============================================================

#include "stdafx.h"
#include "EnigmaHardwareID.h"
#include "EnigmaHardwareIDDlg.h"
#include "config.h"
#include "logger.h"

#include <windows.h>
#include <winioctl.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>   // [O10]
#include <array>    // [O06]

// 存储设备查询结构由 <winioctl.h> 提供，无需重复定义
#ifndef IOCTL_STORAGE_QUERY_PROPERTY
#  define IOCTL_STORAGE_QUERY_PROPERTY \
     CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifdef _DEBUG
#  define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// [O06] 静态 CRC32 查找表定义

std::array<uint32_t, 256> CEnigmaHardwareIDDlg::s_crcTable{};
bool                       CEnigmaHardwareIDDlg::s_crcTableReady = false;

void CEnigmaHardwareIDDlg::EnsureCRCTable()
{
    if (s_crcTableReady) return;
    const uint32_t poly = 0xEDB88320u;
    for (int i = 0; i < 256; i++) {
        uint32_t c = static_cast<uint32_t>(i);
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (poly ^ (c >> 1)) : (c >> 1);
        s_crcTable[i] = c;
    }
    s_crcTableReady = true;
}

// CRC32 计算（直接使用静态表）
static uint32_t CRC32(const void* buf, size_t len)
{
    CEnigmaHardwareIDDlg::EnsureCRCTable();  // 静态方法可从此处调用（public）
    uint32_t c = 0xFFFFFFFFu;
    const auto* u = static_cast<const uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i)
        c = CEnigmaHardwareIDDlg::s_crcTable[(c ^ u[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/////////////////////////////////////////////////////////////////////////////
// [O09] hextobyte — constexpr

static constexpr uint8_t hextobyte(unsigned char ch) noexcept
{
    if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
    if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// CEnigmaHardwareIDDlg 构造

CEnigmaHardwareIDDlg::CEnigmaHardwareIDDlg(CWnd* pParent /*=nullptr*/)
    : CDialog(CEnigmaHardwareIDDlg::IDD, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    // [O01] 成员数组在声明处已用 {} 零初始化，无需 memset
    // [O06] 静态表首次使用时懒初始化
}

void CEnigmaHardwareIDDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CEnigmaHardwareIDDlg, CDialog)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_GETBUTTON,           OnGetbutton)
    ON_BN_CLICKED(IDC_GETTRIALKEY,         OnGettrialkey)
    ON_BN_CLICKED(IDC_EXTRACTRC4KEYFROMRSA,OnExtractrc4keyfromrsa)
    ON_BN_CLICKED(IDC_DECODEHWID,          OnDecodehwid)
    ON_BN_CLICKED(IDC_GENHWID,             OnGenhwid)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// 消息处理函数

BOOL CEnigmaHardwareIDDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    const int checkboxes[] = {
        IDC_COMPNCB, IDC_CPUTCB, IDC_SVSNCB, IDC_MOTHERCB,
        IDC_SVNCB,   IDC_WSNCB,  IDC_HDDSNCB, IDC_USERNCB
    };
    for (int id : checkboxes)
        static_cast<CButton*>(GetDlgItem(id))->SetCheck(BST_CHECKED);

    CheckDlgButton(IDC_CHECKUSED, BST_CHECKED);
    CheckDlgButton(IDC_INSERTSEP, BST_CHECKED);

    // [O06] 提前初始化 CRC 表（避免第一次点击时有微小延迟）
    EnsureCRCTable();

    spdlog::info("OnInitDialog: 对话框初始化完成，所有组件默认勾选");
    return TRUE;
}

void CEnigmaHardwareIDDlg::OnPaint()
{
    if (IsIconic()) {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, (WPARAM)dc.GetSafeHdc(), 0);
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        dc.DrawIcon((rect.Width()  - cxIcon + 1) / 2,
                    (rect.Height() - cyIcon + 1) / 2, m_hIcon);
    } else {
        CDialog::OnPaint();
    }
}

HCURSOR CEnigmaHardwareIDDlg::OnQueryDragIcon()
{
    return (HCURSOR)m_hIcon;
}

/////////////////////////////////////////////////////////////////////////////
// [O04] 辅助：计算 CRC32 并更新 UI 与 m_valuesTable

void CEnigmaHardwareIDDlg::SetComponentCRC(
    int editValId, int editCrcId,
    int tableIdx,
    const void* data, size_t len)
{
    EnsureCRCTable();
    uint32_t crc = CRC32(data, len);
    m_valuesTable[tableIdx] = crc;
    char crcStr[12];
    sprintf_s(crcStr, sizeof(crcStr), "%08X", crc);
    SetDlgItemText(editCrcId, crcStr);
    spdlog::debug("Component[{}] CRC32={}", tableIdx, crcStr);
}

// [O04] 辅助：清除某组件的显示与 CRC
void CEnigmaHardwareIDDlg::ClearComponent(int editValId, int editCrcId, int tableIdx)
{
    SetDlgItemText(editValId, "");
    SetDlgItemText(editCrcId, "");
    m_valuesTable[tableIdx] = 0;
}

/////////////////////////////////////////////////////////////////////////////
// [O05][O10] GetHDDString_ — 获取硬盘型号与序列号（返回 bool）
// [O10] DeviceIoControl 接收缓冲区改为 vector<char>（避免 10 KB 栈帧）

bool CEnigmaHardwareIDDlg::GetHDDString_()
{
    m_HDDStr[0] = '\0';

    char winpath[MAX_PATH + 3];
    GetWindowsDirectoryA(winpath, sizeof(winpath));

    char CPath[MAX_PATH + 3];
    strcpy_s(CPath, sizeof(CPath), "\\\\.\\");
    size_t prefix    = strlen(CPath);
    CPath[prefix]    = winpath[0];
    CPath[prefix+1]  = winpath[1];
    CPath[prefix+2]  = '\0';

    spdlog::debug("GetHDDString_: 打开存储设备路径 '{}'", CPath);

    HANDLE hDev = CreateFileA(CPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        spdlog::warn("GetHDDString_: CreateFile 失败，错误码={}", GetLastError());
        return false;
    }

    // RAII 句柄包装（简单 lambda deleter）
    auto hGuard = std::unique_ptr<void, decltype(&CloseHandle)>(hDev, CloseHandle);

    STORAGE_PROPERTY_QUERY spq{};
    spq.PropertyId = StorageDeviceProperty;
    spq.QueryType  = PropertyStandardQuery;

    // [O10] 先查询所需缓冲区大小
    DWORD dwNeeded = 0;
    DeviceIoControl(hDev, IOCTL_STORAGE_QUERY_PROPERTY,
                    &spq, sizeof(spq), nullptr, 0, &dwNeeded, nullptr);
    if (dwNeeded == 0) dwNeeded = 4096;  // 回退默认大小

    std::vector<char> buf(dwNeeded);
    DWORD dwReturned = 0;
    if (!DeviceIoControl(hDev, IOCTL_STORAGE_QUERY_PROPERTY,
                         &spq, sizeof(spq),
                         buf.data(), static_cast<DWORD>(buf.size()),
                         &dwReturned, nullptr) || dwReturned == 0) {
        spdlog::warn("GetHDDString_: DeviceIoControl 失败，错误码={}", GetLastError());
        return false;
    }

    // 校验偏移量 1（型号）
    int Offset1 = 0;
    memcpy(&Offset1, buf.data() + 0x10, sizeof(int));
    if (Offset1 < 0 || static_cast<DWORD>(Offset1) >= dwReturned) {
        spdlog::warn("GetHDDString_: Offset1={} 越界", Offset1);
        return false;
    }
    strncpy_s(m_HDDStr, sizeof(m_HDDStr), buf.data() + Offset1, _TRUNCATE);

    // 校验偏移量 2（序列号/固件）
    int Offset2 = 0;
    memcpy(&Offset2, buf.data() + 0x18, sizeof(int));
    if (Offset2 < 0 || static_cast<DWORD>(Offset2) >= dwReturned) {
        spdlog::warn("GetHDDString_: Offset2={} 越界", Offset2);
        return false;
    }

    // [O10] std::string 代替固定 10 KB char 数组
    std::string second(buf.data() + Offset2);

    if (second.size() >= 2 &&
        second[0] == '3' && second[1] >= '0' && second[1] <= '9')
    {
        // 十六进制字节序列解码
        std::string bytes;
        bytes.reserve(second.size() / 2);
        int skipped = 0;
        char nibblePrev = 0;
        bool havePrev = false;
        for (int i = 0; i < (int)second.size(); i++) {
            char c = second[i];
            bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            if (!isHex) { skipped++; continue; }
            if (!havePrev) {
                nibblePrev = c;
                havePrev   = true;
            } else {
                bytes.push_back(static_cast<char>(
                    (hextobyte(nibblePrev) << 4) | hextobyte(c)));
                havePrev = false;
            }
        }
        // 截断到非十六进制字符前
        auto endPos = bytes.find_first_not_of("0123456789abcdef");
        if (endPos != std::string::npos) bytes.resize(endPos);
        // 字节对互换
        for (size_t i = 0; i + 1 < bytes.size(); i += 2)
            std::swap(bytes[i], bytes[i+1]);

        strncat_s(m_HDDStr, sizeof(m_HDDStr), bytes.c_str(), _TRUNCATE);
    } else {
        strncat_s(m_HDDStr, sizeof(m_HDDStr), second.c_str(), _TRUNCATE);
    }

    spdlog::info("GetHDDString_ 完成，m_HDDStr='{}'", m_HDDStr);
    return m_HDDStr[0] != '\0';
}

/////////////////////////////////////////////////////////////////////////////
// [O05] GetCPUVendorID_ — 通过 CPUID 获取 CPU 信息（返回 bool）
// [asm] 内联汇编保留不变（VS2017 Win32/x86；x64 需改用 __cpuid 内置函数）

bool CEnigmaHardwareIDDlg::GetCPUVendorID_()
{
    m_CPUVendor[0]   = '\0';
    m_CPUVendorP2[0] = '\0';
    spdlog::debug("GetCPUVendorID_: 执行 CPUID 指令序列");

    static const char AMDCPU[] = "AuthenticAMD";
    unsigned int KeepAddress   = 0;

    // 注意：m_CPUVendor / m_CPUVendorP2 现为成员变量，
    // 内联汇编通过全局符号地址访问——此处用临时局部指针暴露给 asm
    char* pVendor  = m_CPUVendor;
    char* pVendorP2= m_CPUVendorP2;

    _asm
    {
        MOV EDI, pVendor

        MOV EAX,0x0
        CPUID
        MOV DWORD PTR [EDI+0x00], EBX
        MOV DWORD PTR [EDI+0x04], EDX
        MOV DWORD PTR [EDI+0x08], ECX
        MOV EAX,0x1
        CPUID
        MOV EBX,EAX
        AND EAX,0xF
        MOV DWORD PTR [EDI+0x0C],EAX
        SHR EBX,0x4
        MOV EAX,EBX
        AND EAX,0xF
        MOV DWORD PTR [EDI+0x10],EAX
        SHR EBX,0x4
        MOV EAX,EBX
        AND EAX,0xF
        MOV DWORD PTR [EDI+0x14],EAX

        ADD EDI,0x18
        MOV ECX,0x6
        CALL PlaceBytes2

        SHR EDX,1
        MOV ECX,0x3
        CALL PlaceBytes2
        SHR EDX,0x2
        MOV ECX,0x2
        CALL PlaceBytes2
        SHR EDX,1
        MOV ECX,0x1
        CALL PlaceBytes2
        SHR EDX,0x7
        MOV ECX,0x1
        CALL PlaceBytes2

        MOV DWORD PTR KeepAddress[0],EDI

        LEA ESI, AMDCPU
        MOV EDI, pVendor
        MOV ECX,0x0C
        REPE CMPS BYTE PTR [ESI],BYTE PTR [EDI]
        JNZ SkippAMDStuff2

        MOV EAX,0x80000000
        CPUID
        TEST AL,AL
        JE SkippAMDStuff2

        MOV EDI,DWORD PTR KeepAddress[0]

        MOV EAX,0x80000001
        CPUID
        MOV EAX,EDX
        SHR EAX,0xB
        AND AL,0x1
        MOV BYTE PTR DS:[EDI],AL
        MOV EAX,EDX
        SHR EAX,0x10
        AND AL,0x1
        MOV BYTE PTR DS:[EDI+0x1],AL
        MOV EAX,EDX
        SHR EAX,0x1F
        AND AL,0x1
        MOV BYTE PTR DS:[EDI+0x2],AL

        MOV EDI, pVendorP2
        MOV EAX,0x0
        MOV DWORD PTR DS:[EDI],EAX
        MOV EAX,0x80000000
        CPUID
        CMP EAX,0x80000004
        JL SHORT JustCopyString2
        MOV EAX,0x80000002
        CPUID
        MOV DWORD PTR DS:[EDI],    EAX
        MOV DWORD PTR DS:[EDI+0x4],EBX
        MOV DWORD PTR DS:[EDI+0x8],ECX
        MOV DWORD PTR DS:[EDI+0xC],EDX
        ADD EDI,0x10
        MOV EAX,0x80000003
        CPUID
        MOV DWORD PTR DS:[EDI],    EAX
        MOV DWORD PTR DS:[EDI+0x4],EBX
        MOV DWORD PTR DS:[EDI+0x8],ECX
        MOV DWORD PTR DS:[EDI+0xC],EDX
        ADD EDI,0x10
        MOV EAX,0x80000004
        CPUID
        MOV DWORD PTR DS:[EDI],    EAX
        MOV DWORD PTR DS:[EDI+0x4],EBX
        MOV DWORD PTR DS:[EDI+0x8],ECX
        MOV DWORD PTR DS:[EDI+0xC],EDX

    JustCopyString2:
        MOV ESI, pVendorP2
        MOV EDI, pVendor
        ADD EDI,0x28
        MOV ECX,0x30
        REP MOVS BYTE PTR ES:[EDI],BYTE PTR DS:[ESI]

    SkippAMDStuff2:
        JMP End2

    PlaceBytes2:
        MOV AL,DL
        AND AL,0x1
        MOV BYTE PTR DS:[EDI],AL
        SHR EDX,1
        INC EDI
        LOOP PlaceBytes2
        RETN

    End2:
    }

    spdlog::debug("GetCPUVendorID_ 完成，Vendor='{:.12s}'", m_CPUVendor);
    return m_CPUVendor[0] != '\0';
}

/////////////////////////////////////////////////////////////////////////////
// [O07] GetRegistryKeyValue — RAII 注册表读取，修复原版 bug
// 原版：RegOpenKeyEx 失败时仍调用 RegCloseKey(未初始化的 Registry 变量)
// 修复：先检查返回值，失败直接返回 nullptr，不关闭无效句柄

static std::unique_ptr<byte[]> GetRegistryKeyValue(
    const char* RegKey, const char* pPIDName)
{
    HKEY  hReg = nullptr;
    DWORD ulOptions = KEY_QUERY_VALUE;

    // IsWow64 检测
    {
        BOOL bWow = FALSE;
        typedef BOOL(APIENTRY* FnIsWow64)(HANDLE, PBOOL);
        HMODULE hK32 = GetModuleHandle(_T("kernel32"));
        auto fn = reinterpret_cast<FnIsWow64>(
            GetProcAddress(hK32, "IsWow64Process"));
        if (fn && fn(GetCurrentProcess(), &bWow) && bWow)
            ulOptions |= KEY_WOW64_64KEY;
    }

    long rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, RegKey, 0, ulOptions, &hReg);
    if (rc != ERROR_SUCCESS) {
        // [O07] 不调用 RegCloseKey：句柄未成功打开
        spdlog::warn("GetRegistryKeyValue: RegOpenKeyExA('{}') 失败，code={}", RegKey, rc);
        return nullptr;
    }

    DWORD regType = 0, regSize = 0;
    RegQueryValueExA(hReg, pPIDName, NULL, &regType, nullptr, &regSize);

    if (regSize == 0) {
        RegCloseKey(hReg);
        spdlog::warn("GetRegistryKeyValue: '{}' 大小为 0", pPIDName);
        return nullptr;
    }

    auto pPID = std::make_unique<byte[]>(regSize + 1);
    pPID[regSize] = '\0';
    RegQueryValueExA(hReg, pPIDName, NULL, nullptr,
                     reinterpret_cast<LPBYTE>(pPID.get()), &regSize);
    RegCloseKey(hReg);

    // 修整末尾非打印字符
    if (regSize > 0 && (pPID[regSize-1] > 127 || pPID[regSize-1] < 32))
        pPID[regSize-1] = '\0';

    spdlog::debug("GetRegistryKeyValue('{}') 成功，读取 {} 字节", pPIDName, regSize);
    return pPID;
}

/////////////////////////////////////////////////////////////////////////////
// [O05] GetWindowSerial_ — 读取 Windows 产品密钥（返回 bool）

bool CEnigmaHardwareIDDlg::GetWindowSerial_()
{
    m_WindowsSerial[0] = '\0';
    spdlog::debug("GetWindowSerial_: 读取注册表 DigitalProductId");

    auto spPID = GetRegistryKeyValue(
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        "DigitalProductId");
    if (!spPID) {
        spdlog::warn("GetWindowSerial_: 无法读取 DigitalProductId");
        return false;
    }

    const uint8_t* digitalProductId = spPID.get();
    const int keyStartIndex   = 52;
    const int keyEndIndex     = keyStartIndex + 15;
    const int decodeLength    = 29;
    const int decodeStringLen = 15;
    static const char digits[] = {
        'B','C','D','F','G','H','J','K','M','P','Q','R',
        'T','V','W','X','Y','2','3','4','6','7','8','9',
    };

    char pDecodedChars[decodeLength + 1];
    memset(pDecodedChars, 0, sizeof(pDecodedChars));

    byte hexPid[keyEndIndex - keyStartIndex + 1];
    for (int i = keyStartIndex; i <= keyEndIndex; i++)
        hexPid[i - keyStartIndex] = digitalProductId[i];

    for (int i = decodeLength - 1; i >= 0; i--) {
        if ((i + 1) % 6 == 0) {
            pDecodedChars[i] = '-';
        } else {
            int digitMapIndex = 0;
            for (int j = decodeStringLen - 1; j >= 0; j--) {
                int byteValue    = (digitMapIndex << 8) | hexPid[j];
                hexPid[j]        = static_cast<byte>(byteValue / 24);
                digitMapIndex    = byteValue % 24;
                pDecodedChars[i] = digits[digitMapIndex];
            }
        }
    }

    strncpy_s(m_WindowsSerial, sizeof(m_WindowsSerial), pDecodedChars, _TRUNCATE);
    spdlog::info("GetWindowSerial_ 成功，序列号='{}'", m_WindowsSerial);
    return m_WindowsSerial[0] != '\0';
}

/////////////////////////////////////////////////////////////////////////////
// SMBIOS 结构定义

typedef UINT(WINAPI* PGETSYSTEMFIRMWARETABLE)(DWORD, DWORD, PVOID, DWORD);

struct SMBIOSHEADER {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
};
struct SMBIOSData {
    uint8_t  Used20CallingMethod;
    uint8_t  SMBIOSMajorVersion;
    uint8_t  SMBIOSMinorVersion;
    uint8_t  DmiRevision;
    uint32_t Length;
    uint8_t  SMBIOSTableData[1];
};
struct SYSTEMINFORMATION {
    SMBIOSHEADER Header;
    uint8_t Manufacturer;
    uint8_t ProductName;
    uint8_t Version;
    uint8_t SerialNumber;
    uint8_t UUID[16];
    uint8_t WakeUpType;
    uint8_t SKUNumber;
    uint8_t Family;
};

static SYSTEMINFORMATION* find_system_information(SMBIOSData* bios_data)
{
    uint8_t* data = bios_data->SMBIOSTableData;
    while (data < bios_data->SMBIOSTableData + bios_data->Length) {
        auto* hdr = reinterpret_cast<SMBIOSHEADER*>(data);
        if (hdr->length < 4) break;
        if (hdr->type == 0x01 && hdr->length >= 0x19)
            return reinterpret_cast<SYSTEMINFORMATION*>(hdr);
        uint8_t* next = data + hdr->length;
        while (next < bios_data->SMBIOSTableData + bios_data->Length &&
               (next[0] != 0 || next[1] != 0))
            next++;
        data = next + 2;
    }
    return nullptr;
}

static const char* get_string_by_index(const char* str, int index,
                                        const char* null_text = "")
{
    if (index == 0 || *str == '\0') return null_text;
    while (--index) str += strlen(str) + 1;
    return str;
}

/////////////////////////////////////////////////////////////////////////////
// [O05] GetMotherboard_ — 通过 SMBIOS 获取主板信息（返回 bool）

bool CEnigmaHardwareIDDlg::GetMotherboard_()
{
    m_Motherboard[0] = '\0';
    spdlog::debug("GetMotherboard_: 查询 SMBIOS 固件表");

    auto pGetSFT = reinterpret_cast<PGETSYSTEMFIRMWARETABLE>(
        GetProcAddress(GetModuleHandle("kernel32.dll"), "GetSystemFirmwareTable"));
    if (!pGetSFT) {
        spdlog::warn("GetMotherboard_: GetSystemFirmwareTable 不可用（需 XP SP2+）");
        return false;
    }

    DWORD dwSize = pGetSFT('RSMB', 0, NULL, 0);
    if (!dwSize) {
        spdlog::warn("GetMotherboard_: SMBIOS 大小查询返回 0");
        return false;
    }

    std::unique_ptr<BYTE[]> pBuf(new BYTE[dwSize]);
    pGetSFT('RSMB', 0, pBuf.get(), dwSize);

    auto* bios_data = reinterpret_cast<SMBIOSData*>(pBuf.get());
    SYSTEMINFORMATION* si = find_system_information(bios_data);
    if (!si) {
        spdlog::warn("GetMotherboard_: 未找到 SMBIOS Type 1 结构");
        return false;
    }

    const char* str = reinterpret_cast<const char*>(si) + si->Header.length;
    std::string sb;
    sb.reserve(512);
    sb += get_string_by_index(str, si->Manufacturer);
    sb += get_string_by_index(str, si->ProductName);
    sb += get_string_by_index(str, si->Version);
    sb += get_string_by_index(str, si->SerialNumber);

    if (si->Header.length > 0x08) {
        char uuid[50];
        sprintf_s(uuid, sizeof(uuid),
            "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
            si->UUID[0],  si->UUID[1],  si->UUID[2],  si->UUID[3],
            si->UUID[4],  si->UUID[5],  si->UUID[6],  si->UUID[7],
            si->UUID[8],  si->UUID[9],  si->UUID[10], si->UUID[11],
            si->UUID[12], si->UUID[13], si->UUID[14], si->UUID[15]);
        sb += uuid;
    }
    if (si->Header.length > 0x19) {
        sb += get_string_by_index(str, si->SKUNumber);
        sb += get_string_by_index(str, si->Family);
    }

    strncpy_s(m_Motherboard, sizeof(m_Motherboard), sb.c_str(), _TRUNCATE);
    spdlog::info("GetMotherboard_ 成功，原始长度={}，存储长度={}",
                 sb.size(), strlen(m_Motherboard));
    return m_Motherboard[0] != '\0';
}

/////////////////////////////////////////////////////////////////////////////
// [O02][O03] InitializeRC4Key — a/b/swap 改为纯局部变量

void CEnigmaHardwareIDDlg::InitializeRC4Key(const uint8_t* pKey, unsigned int lenKey)
{
    spdlog::debug("InitializeRC4Key: 初始化 S-Box，密钥长度={}", lenKey);
    for (int i = 0; i < 256; i++) m_sBox[i] = static_cast<uint8_t>(i);
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = static_cast<uint8_t>((j + m_sBox[i] + pKey[i % lenKey]) & 0xFF);
        std::swap(m_sBox[i], m_sBox[j]);
    }
}

// [O02][O08] RC4 — S-Box 拷贝从 int[256] → uint8_t[256]（节省 768 字节）
void CEnigmaHardwareIDDlg::RC4(uint8_t* pData, unsigned long lenData)
{
    std::array<uint8_t, 256> sBox = m_sBox;   // [O08] 256 字节而非 1024 字节
    uint8_t i = 0, j = 0;
    for (unsigned long offset = 0; offset < lenData; offset++) {
        i = static_cast<uint8_t>((i + 1) & 0xFF);
        j = static_cast<uint8_t>((j + sBox[i]) & 0xFF);
        std::swap(sBox[i], sBox[j]);
        pData[offset] ^= sBox[static_cast<uint8_t>((sBox[i] + sBox[j]) & 0xFF)];
    }
}

/////////////////////////////////////////////////////////////////////////////
// [O12] 辅助：从 IDC_RC4Key 控件加载 m_sBox

static bool LoadSBoxFromEdit(CEnigmaHardwareIDDlg* pDlg,
                              std::array<uint8_t, 256>& sBox,
                              int editId = IDC_RC4Key)
{
    char RC4KeyStr[256 * 2 + 1];
    CWnd* w = pDlg->GetDlgItem(editId);
    if (!w) return false;
    ::GetWindowTextA(w->m_hWnd, RC4KeyStr, sizeof(RC4KeyStr));
    if (RC4KeyStr[0] == '\0') return false;

    int len = static_cast<int>(strlen(RC4KeyStr));
    for (int i = 0; i + 1 < len; i += 2)
        sBox[i / 2] = static_cast<uint8_t>(
            (hextobyte(RC4KeyStr[i]) << 4) | hextobyte(RC4KeyStr[i+1]));
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// [O04] OnGetbutton — 采集所有硬件指纹

#define INFO_BUFFER_SIZE 32767
static WCHAR g_ComputerName[INFO_BUFFER_SIZE];
static WCHAR g_UserName[INFO_BUFFER_SIZE];

void CEnigmaHardwareIDDlg::OnGetbutton()
{
    spdlog::info("=== 开始采集硬件指纹 ===");
    EnsureCRCTable();

    // ── 计算机名 ─────────────────────────────────────────────
    {
        DWORD cnt = INFO_BUFFER_SIZE;
        if (IsDlgButtonChecked(IDC_COMPNCB) && GetComputerNameW(g_ComputerName, &cnt)) {
            SetDlgItemTextW(m_hWnd, IDC_EDIT1, g_ComputerName);
            SetComponentCRC(IDC_EDIT1, IDC_EDIT2, 0,
                            g_ComputerName, wcslen(g_ComputerName) * 2);
        } else {
            ClearComponent(IDC_EDIT1, IDC_EDIT2, 0);
        }
    }

    // ── CPU 信息 ─────────────────────────────────────────────
    if (IsDlgButtonChecked(IDC_CPUTCB) && GetCPUVendorID_()) {
        char info[4000];
        strcpy_s(info, sizeof(info), m_CPUVendor);
        strncat_s(info, sizeof(info), "..", _TRUNCATE);
        strncat_s(info, sizeof(info), m_CPUVendorP2, _TRUNCATE);
        SetDlgItemText(IDC_EDIT3, info);
        SetComponentCRC(IDC_EDIT3, IDC_EDIT4, 1, m_CPUVendor, 0x58);
    } else {
        ClearComponent(IDC_EDIT3, IDC_EDIT4, 1);
    }

    // ── 系统卷序列号 ─────────────────────────────────────────
    {
        char winpath[MAX_PATH + 3], CPath[20];
        GetWindowsDirectoryA(winpath, sizeof(winpath));
        CPath[0] = winpath[0]; CPath[1] = winpath[1];
        CPath[2] = winpath[2]; CPath[3] = '\0';
        DWORD volSN = 0;
        if (IsDlgButtonChecked(IDC_SVSNCB) &&
            GetVolumeInformationA(CPath, NULL, 0, &volSN, NULL, NULL, NULL, 0)) {
            char snStr[20];
            wsprintf(snStr, "%08X", volSN);
            SetDlgItemTextA(IDC_EDIT5, snStr);
            m_valuesTable[2] = volSN;
            spdlog::debug("VolumeSerial 值={}", snStr);
        } else {
            ClearComponent(IDC_EDIT5, IDC_EDIT5, 2);   // 无 CRC 控件，复用
            SetDlgItemText(IDC_EDIT5, "");
            m_valuesTable[2] = 0;
        }
    }

    // ── 主板信息 ─────────────────────────────────────────────
    if (IsDlgButtonChecked(IDC_MOTHERCB)) {
        if (IsDlgButtonChecked(IDC_0x200EMPTY)) {
            char empty[0x200]{};
            SetDlgItemText(IDC_EDIT6, "");
            SetComponentCRC(IDC_EDIT6, IDC_EDIT7, 3, empty, 0x200);
            spdlog::debug("Motherboard CRC32={:08X} (空填充模式)", m_valuesTable[3]);
        } else if (GetMotherboard_()) {
            SetDlgItemText(IDC_EDIT6, m_Motherboard);
            SetComponentCRC(IDC_EDIT6, IDC_EDIT7, 3,
                            m_Motherboard, strlen(m_Motherboard));
        } else {
            ClearComponent(IDC_EDIT6, IDC_EDIT7, 3);
        }
    } else {
        ClearComponent(IDC_EDIT6, IDC_EDIT7, 3);
    }

    // ── 卷名 ─────────────────────────────────────────────────
    {
        WCHAR winpathU[MAX_PATH + 3], CPathU[8];
        GetWindowsDirectoryW(winpathU, MAX_PATH);
        CPathU[0] = winpathU[0]; CPathU[1] = winpathU[1];
        CPathU[2] = winpathU[2]; CPathU[3] = 0;
        WCHAR volName[MAX_PATH]{};
        if (IsDlgButtonChecked(IDC_SVNCB) &&
            GetVolumeInformationW(CPathU, volName, MAX_PATH,
                                  NULL, NULL, NULL, NULL, 0)) {
            SetDlgItemTextW(m_hWnd, IDC_EDIT8, volName);
            SetComponentCRC(IDC_EDIT8, IDC_EDIT9, 4,
                            volName, wcslen(volName));
        } else {
            ClearComponent(IDC_EDIT8, IDC_EDIT9, 4);
        }
    }

    // ── Windows 序列号 ───────────────────────────────────────
    if (IsDlgButtonChecked(IDC_WSNCB) && GetWindowSerial_()) {
        SetDlgItemText(IDC_EDIT10, m_WindowsSerial);
        SetComponentCRC(IDC_EDIT10, IDC_EDIT11, 5,
                        m_WindowsSerial, strlen(m_WindowsSerial));
    } else {
        ClearComponent(IDC_EDIT10, IDC_EDIT11, 5);
    }

    // ── 硬盘序列号 ───────────────────────────────────────────
    if (IsDlgButtonChecked(IDC_HDDSNCB) && GetHDDString_()) {
        SetDlgItemText(IDC_EDIT12, m_HDDStr);
        SetComponentCRC(IDC_EDIT12, IDC_EDIT13, 6,
                        m_HDDStr, strlen(m_HDDStr));
    } else {
        ClearComponent(IDC_EDIT12, IDC_EDIT13, 6);
    }

    // ── 用户名 ───────────────────────────────────────────────
    {
        DWORD cnt2 = INFO_BUFFER_SIZE;
        if (IsDlgButtonChecked(IDC_USERNCB) && GetUserNameW(g_UserName, &cnt2)) {
            CharLowerBuffW(g_UserName, static_cast<DWORD>(wcslen(g_UserName)));
            SetDlgItemTextW(m_hWnd, IDC_EDIT14, g_UserName);
            SetComponentCRC(IDC_EDIT14, IDC_EDIT15, 7,
                            g_UserName, (wcslen(g_UserName) + 1) * 2);
        } else {
            ClearComponent(IDC_EDIT14, IDC_EDIT15, 7);
        }
    }

    spdlog::info("=== 硬件指纹采集完成，m_valuesTable[0..7] 已填充 ===");
}

/////////////////////////////////////////////////////////////////////////////
// OnGettrialkey

void CEnigmaHardwareIDDlg::OnGettrialkey()
{
    spdlog::info("OnGettrialkey: 加载 Demo RSA Key（来自 config.h）");
    SetDlgItemText(IDC_RSAKEY, ENIGMA_DEMO_RSA_KEY);
}

/////////////////////////////////////////////////////////////////////////////
// 字符串编解码辅助

static const char AllowedChars[] = "ABCDEF1234567890- \r\n";

static char CharToIndex(char tch)
{
    int len = static_cast<int>(strlen(AllowedChars));
    for (int j = 0; j < len; j++)
        if (tch == AllowedChars[j]) return static_cast<char>(j);
    return -1;
}

int CEnigmaHardwareIDDlg::DecodeString(char* ToDecode, char* destination)
{
    char  charindexP  = 0;
    bool  WasPrevious = false;
    int   dindx = 0;
    int   len   = static_cast<int>(strlen(ToDecode));

    for (int i = 0; i < len; i++) {
        char char1index = CharToIndex(ToDecode[i]);
        if (char1index < 0) {
            SetDlgItemText(IDC_RC4Key, "Invalid char!");
            spdlog::warn("DecodeString: 非法字符 '{}' at pos {}", ToDecode[i], i);
            return 0;
        }
        if (char1index > 0x0F) continue;
        if (!WasPrevious) {
            charindexP  = char1index;
            WasPrevious = true;
        } else {
            destination[dindx++] = static_cast<char>(charindexP * 16 + char1index);
            WasPrevious = false;
        }
    }
    return dindx;
}

// [O13] EncodeToString — 修正分隔符插入逻辑
// 原版：使用 2*i+insertCount 作为输出偏移，但 insertCount 的增长与
//       (2*i)%addSep 条件不同步，导致 insertCount 较大时分隔符位置偏移。
// 修正：使用独立 outPos 变量追踪输出位置，逻辑更清晰。
int CEnigmaHardwareIDDlg::EncodeToString(char* ToEncode, int enlen, char* destination)
{
    int addSep = 0;
    if (IsDlgButtonChecked(IDC_INSERTSEP)) {
        if      ((enlen * 2) % 6 == 0) addSep = 6;
        else if ((enlen * 2) % 5 == 0) addSep = 5;
    }

    int outPos = 0;
    int nibbleCount = 0;  // 已写入的有效十六进制字符数（不含分隔符）

    for (int i = 0; i < enlen; i++) {
        // 高半字节
        if (addSep != 0 && nibbleCount != 0 && nibbleCount % addSep == 0)
            destination[outPos++] = '-';
        destination[outPos++] = AllowedChars[(ToEncode[i] & 0xF0) >> 4];
        nibbleCount++;

        // 低半字节
        if (addSep != 0 && nibbleCount % addSep == 0)
            destination[outPos++] = '-';
        destination[outPos++] = AllowedChars[ToEncode[i] & 0xF];
        nibbleCount++;
    }
    destination[outPos] = '\0';
    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// ExtractRC4EncryptionKeyFromRsa
// [O11] 增加 KeyLen 范围检查

void CEnigmaHardwareIDDlg::ExtractRC4EncryptionKeyFromRsa()
{
    CWnd* hWndRSAKey = GetDlgItem(IDC_RSAKEY);
    char RSAKey[MAX_PATH];
    ::GetWindowTextA(hWndRSAKey->m_hWnd, RSAKey, MAX_PATH);
    if (RSAKey[0] == '\0') return;

    char KeyLenStr[5];
    memcpy(KeyLenStr, RSAKey, 3);
    KeyLenStr[3] = '\0';

    std::istringstream conv(KeyLenStr);
    unsigned int KeyLen = 0;
    conv >> std::hex >> KeyLen;

    // [O11] KeyLen 必须在合理范围内
    if (KeyLen == 0 || KeyLen >= static_cast<unsigned int>(MAX_PATH - 3)) {
        SetDlgItemText(IDC_RC4Key, "Invalid key length!");
        spdlog::error("ExtractRC4: KeyLen={} 非法", KeyLen);
        return;
    }
    spdlog::debug("ExtractRC4: RSA Key 数据长度={}", KeyLen);

    memcpy(RSAKey, RSAKey + 3, KeyLen);
    RSAKey[KeyLen] = '\0';

    char keydest[0x40];
    int declen = DecodeString(RSAKey, keydest);
    if (declen <= 0) {
        SetDlgItemText(IDC_RC4Key, "Failed to decode!");
        spdlog::error("ExtractRC4: DecodeString 失败");
        return;
    }

    InitializeRC4Key(reinterpret_cast<const uint8_t*>(keydest),
                     static_cast<unsigned int>(declen));

    char hexKey[256 * 2 + 1];
    for (int i = 0; i < 256; i++)
        sprintf_s(hexKey + i * 2, 3, "%.2X",
                  static_cast<unsigned char>(m_sBox[i]));
    hexKey[512] = '\0';
    SetDlgItemText(IDC_RC4Key, hexKey);
    spdlog::info("RC4 密钥提取成功（前 16 字节: {:.16s}...）", hexKey);
}

void CEnigmaHardwareIDDlg::OnExtractrc4keyfromrsa()
{
    ExtractRC4EncryptionKeyFromRsa();
}

/////////////////////////////////////////////////////////////////////////////
// [O12] OnDecodehwid — 解码 HWID

void CEnigmaHardwareIDDlg::OnDecodehwid()
{
    spdlog::info("=== 开始解码 HWID ===");

    char HARDWAREID[MAX_PATH];
    {
        CWnd* w = GetDlgItem(IDC_HARDWAREID);
        ::GetWindowTextA(w->m_hWnd, HARDWAREID, MAX_PATH);
        if (HARDWAREID[0] == '\0') return;
    }

    // [O12] 统一从 RC4Key 控件加载 S-Box
    if (!LoadSBoxFromEdit(this, m_sBox)) {
        SetDlgItemText(IDC_INFO, "RC4 key can't be empty!");
        spdlog::warn("OnDecodehwid: RC4 密钥为空");
        return;
    }

    // 清空所有显示字段
    const int clearIDs[] = { IDC_EDIT1, IDC_EDIT3, IDC_EDIT5, IDC_EDIT6,
                              IDC_EDIT8, IDC_EDIT10, IDC_EDIT12, IDC_EDIT14 };
    for (int id : clearIDs) SetDlgItemText(id, "");

    char decodestr[MAX_PATH];
    int  declen1 = DecodeString(HARDWAREID, decodestr);

    uint16_t CRCVal =
        (static_cast<uint8_t>(decodestr[1]) << 8) |
         static_cast<uint8_t>(decodestr[0]);

    RC4(reinterpret_cast<uint8_t*>(decodestr) + 2,
        static_cast<unsigned long>(declen1 - 2));

    EnsureCRCTable();
    uint32_t CRC = CRC32(decodestr + 2, declen1 - 2);

    if ((CRC & 0xFFFFu) != CRCVal) {
        SetDlgItemText(IDC_INFO, "CRC failed!");
        spdlog::error("OnDecodehwid: CRC 失败，期望={:04X}，实际={:04X}",
                      CRCVal, CRC & 0xFFFFu);
        return;
    }
    spdlog::info("OnDecodehwid: CRC {:04X} 校验通过", CRCVal);

    // 显示原始字节
    char decdata[256 * 2 + 1];
    for (int i = 0; i < declen1; i++)
        sprintf_s(decdata + i * 2, 3, "%.2X",
                  static_cast<uint8_t>(decodestr[i]));
    decdata[declen1 * 2] = '\0';
    SetDlgItemText(IDC_EDIT16, decdata);

    // Lambda 解析各组件位
    int srcindex = 0;
    auto readWord = [&](int editId, int checkboxId, uint8_t bit) {
        if (decodestr[2] & bit) {
            char buf[5];
            sprintf_s(buf,     3, "%.2X",
                      static_cast<uint8_t>(decodestr[2 + 2 + srcindex * 2 + 1]));
            sprintf_s(buf + 2, 3, "%.2X",
                      static_cast<uint8_t>(decodestr[2 + 2 + srcindex * 2]));
            buf[4] = '\0';
            SetDlgItemText(editId, buf);
            srcindex++;
            if (IsDlgButtonChecked(IDC_CHECKUSED))
                CheckDlgButton(checkboxId, BST_CHECKED);
        } else if (IsDlgButtonChecked(IDC_CHECKUSED)) {
            CheckDlgButton(checkboxId, BST_UNCHECKED);
        }
    };

    readWord(IDC_EDIT5,  IDC_SVSNCB,   0x01);
    readWord(IDC_EDIT9,  IDC_SVNCB,    0x02);
    readWord(IDC_EDIT4,  IDC_CPUTCB,   0x04);
    readWord(IDC_EDIT2,  IDC_COMPNCB,  0x08);
    readWord(IDC_EDIT7,  IDC_MOTHERCB, 0x10);
    readWord(IDC_EDIT11, IDC_WSNCB,    0x20);
    readWord(IDC_EDIT13, IDC_HDDSNCB,  0x40);
    readWord(IDC_EDIT15, IDC_USERNCB,  0x80);

    spdlog::info("=== HWID 解码完成，共 {} 个有效组件 ===", srcindex);
}

/////////////////////////////////////////////////////////////////////////////
// GetWordFromString

static bool GetWordFromString(const char* str, char* data, int datapos)
{
    if (str[0] == '\0') return false;
    int len = static_cast<int>(strlen(str));
    int idx = (len > 4) ? (len - 4) : 0;
    std::istringstream conv(str + idx);
    unsigned int intData = 0;
    conv >> std::hex >> intData;
    data[datapos]     = static_cast<char>( intData        & 0xFF);
    data[datapos + 1] = static_cast<char>((intData >> 8)  & 0xFF);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// [O12] OnGenhwid — 生成 HWID

void CEnigmaHardwareIDDlg::OnGenhwid()
{
    spdlog::info("=== 开始生成 HWID ===");

    char data[2 + 2 + 2 * 8]{};
    char varstring[MAX_PATH];
    int elementsCount = 0;

    // Lambda 打包各组件
    auto packComponent = [&](int checkboxId, int editId, uint8_t bit) {
        varstring[0] = '\0';
        if (IsDlgButtonChecked(checkboxId)) {
            CWnd* w = GetDlgItem(editId);
            ::GetWindowTextA(w->m_hWnd, varstring, MAX_PATH);
            if (GetWordFromString(varstring, data, 4 + elementsCount * 2)) {
                elementsCount++;
                data[2] |= static_cast<char>(bit);
            }
        }
    };

    packComponent(IDC_SVSNCB,   IDC_EDIT5,  0x01);
    packComponent(IDC_SVNCB,    IDC_EDIT9,  0x02);
    packComponent(IDC_CPUTCB,   IDC_EDIT4,  0x04);
    packComponent(IDC_COMPNCB,  IDC_EDIT2,  0x08);
    packComponent(IDC_MOTHERCB, IDC_EDIT7,  0x10);
    packComponent(IDC_WSNCB,    IDC_EDIT11, 0x20);
    packComponent(IDC_HDDSNCB,  IDC_EDIT13, 0x40);
    packComponent(IDC_USERNCB,  IDC_EDIT15, 0x80);

    spdlog::debug("OnGenhwid: 打包 {} 个组件，位掩码=0x{:02X}",
                  elementsCount, static_cast<uint8_t>(data[2]));

    // CRC-16（取 CRC32 低 16 位）
    EnsureCRCTable();
    int datalen = 4 + elementsCount * 2;
    uint32_t CRC = CRC32(data + 2, 2 + elementsCount * 2) & 0xFFFFu;
    data[0] = static_cast<char>( CRC        & 0xFF);
    data[1] = static_cast<char>((CRC >> 8)  & 0xFF);

    // 显示原始字节
    char decdata[256 * 2 + 1];
    for (int i = 0; i < datalen; i++)
        sprintf_s(decdata + i * 2, 3, "%.2X",
                  static_cast<uint8_t>(data[i]));
    decdata[datalen * 2] = '\0';
    SetDlgItemText(IDC_EDIT16, decdata);

    // [O12] 统一加载 S-Box
    if (!LoadSBoxFromEdit(this, m_sBox)) {
        SetDlgItemText(IDC_INFO, "RC4 key can't be empty!");
        spdlog::warn("OnGenhwid: RC4 密钥为空");
        return;
    }

    // RC4 加密（跳过前 2 字节 CRC）
    RC4(reinterpret_cast<uint8_t*>(data) + 2,
        static_cast<unsigned long>(datalen - 2));

    // 编码为可打印字符串
    char HWID[256]{};
    EncodeToString(data, datalen, HWID);
    if (HWID[0]) SetDlgItemText(IDC_HARDWAREID, HWID);

    spdlog::info("=== HWID 生成完成: '{}' ===", HWID);
}
