// config.h — EnigmaHardwareID 可配置常量
//
// 本文件可安全提交到版本控制（不含私密数据）。
// 如需覆盖任何值，请在 config.h 同目录下新建 config_local.h，
// 并在其中用 #undef / #define 覆盖对应宏。
// config_local.h 已加入 .gitignore，不会被意外提交。
//

#pragma once

// ── Demo RSA Key ─────────────────────────────────────────────
// Enigma Protector Demo 版本的示例 RSA 公钥字符串。
// 格式：3 位十六进制长度前缀 + Base36 编码密钥体。
// 此默认值来自 Enigma Protector 官方 Demo，可公开。
#ifndef ENIGMA_DEMO_RSA_KEY
#define ENIGMA_DEMO_RSA_KEY \
    "0201B810DA4A1ADD4351378790A98138533067CP4S86R7D8THS45GBCVUM635EPRQR" \
    "MYRP3DAA5DUPZ6ABDSFP7F5ACP7ERGH4A7Y6B6NW6NMMBZF83WVER9Y4MMBNLBQDK" \
    "R7KFVLGLV067CFDQCWCHGQVVRN24DECEPBL96YJQJTVDCRTNQG3E4WW4GK4GQ5X5L" \
    "5H88D3XYHCBRBNASPD3P5CNYFKFHBCSDHHD6WPTCC4XVSM5S88067C2JSTCMVT48C" \
    "8HC7SHKGTFJBM28P6XTBCNWHMV6J6KN6W5Q9TQLVR285U6GVCAAUTZLRTPSRGDQ74" \
    "2B4742XF4MACRR747YDP5FZZ9D"
#endif

// ── 覆盖用户配置（可选）──────────────────────────────────────
// 如存在 config_local.h，将其宏覆盖上面的默认值
#ifdef __has_include
#  if __has_include("config_local.h")
#    include "config_local.h"
#  endif
#endif
