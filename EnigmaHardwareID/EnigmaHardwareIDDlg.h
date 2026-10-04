// EnigmaHardwareIDDlg.h — 对话框头文件（优化版 v2，VS2017+）
// ============================================================
// 相对 v1 的变更（v2 新增）：
//   [O01] 消除全局变量：HDDStr/CPUVendor/Motherboard/WindowsSerial/ValuesTable
//         → 全部迁移为 CEnigmaHardwareIDDlg 私有成员，线程更安全
//   [O02] m_sBox 类型修正：int[256] → uint8_t[256]（节省 3×256 字节）
//   [O03] a/b/swap 冗余成员变量拆除（InitializeRC4Key 局部变量已够用）
//   [O04] 增加 SetComponentCRC / ClearComponent 内联辅助声明，消除 .cpp 重复代码
//   [O05] 为所有硬件采集函数增加 [[nodiscard]] 返回值语义（bool 表示成功与否）
//   [O06] 将 CRC 查找表改为静态成员，避免 OnGetbutton/OnGenhwid/OnDecodehwid
//         每次都在栈上重建 256 个 uint32_t
// ============================================================
#pragma once

#include <array>
#include <string>
#include <cstdint>

/////////////////////////////////////////////////////////////////////////////
// CEnigmaHardwareIDDlg — 硬件指纹对话框

class CEnigmaHardwareIDDlg : public CDialog
{
// ── 构造 ──────────────────────────────────────────────────────────────────
public:
    explicit CEnigmaHardwareIDDlg(CWnd* pParent = nullptr);

// ── 对话框数据 ────────────────────────────────────────────────────────────
    enum { IDD = IDD_ENIGMAHARDWAREID_DIALOG };

// ── 公开接口 ──────────────────────────────────────────────────────────────
    void ExtractRC4EncryptionKeyFromRsa();
    void InitializeRC4Key(const uint8_t* pKey, unsigned int lenKey);  // [O02]
    void RC4(uint8_t* pData, unsigned long lenData);                  // [O02]

    int  DecodeString (char* ToDecode, char* destination);
    int  EncodeToString(char* ToEncode, int enlen, char* destination);

    // [O06] CRC 查找表 — public 以便全局 CRC32() 辅助函数访问
    static std::array<uint32_t, 256> s_crcTable;
    static bool                       s_crcTableReady;
    static void EnsureCRCTable();

// ── 重写 ──────────────────────────────────────────────────────────────────
protected:
    virtual void DoDataExchange(CDataExchange* pDX);

// ── 实现 ──────────────────────────────────────────────────────────────────
protected:
    HICON   m_hIcon;

    // [O02] S-Box 用 uint8_t，节省空间；不再需要 a/b/swap 成员 [O03]
    std::array<uint8_t, 256> m_sBox{};

    // [O01] 硬件信息字符串：原全局变量 → 私有成员
    char m_HDDStr       [MAX_PATH + 3]{};
    char m_CPUVendor    [0x258]       {};
    char m_CPUVendorP2  [0x58]        {};
    char m_Motherboard  [MAX_PATH + 3]{};
    char m_WindowsSerial[30]          {};   // 29 字符 + NUL

    // [O01] CRC 结果表：原全局变量 → 私有成员
    std::array<uint32_t, 8> m_valuesTable{};

    // ── 硬件采集（[O05] 返回 bool 表示是否成功）──────────────
    bool GetHDDString_();
    bool GetCPUVendorID_();
    bool GetMotherboard_();
    bool GetWindowSerial_();

    // ── UI 更新辅助（[O04] 消除重复 SetDlgItemText 代码）─────
    void SetComponentCRC (int editValId, int editCrcId,
                          int tableIdx,
                          const void* data, size_t len);
    void ClearComponent  (int editValId, int editCrcId, int tableIdx);

    // ── 消息处理 ──────────────────────────────────────────────
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnGetbutton();
    afx_msg void OnGettrialkey();
    afx_msg void OnExtractrc4keyfromrsa();
    afx_msg void OnDecodehwid();
    afx_msg void OnGenhwid();

    DECLARE_MESSAGE_MAP()
};
