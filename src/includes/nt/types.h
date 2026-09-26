#pragma once
#include <cstdint>

// Windows x64 呼び出し規約．PE側から呼ばれる関数・PE側の関数ポインタには必ず付ける
#define WINAPI __attribute__((ms_abi))

namespace ntmac::nt {

using BOOL = int32_t;
using DWORD = uint32_t;
using UINT = uint32_t;
using HANDLE = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPDWORD = DWORD*;

constexpr BOOL kTrue = 1;
constexpr BOOL kFalse = 0;

inline const HANDLE kInvalidHandleValue = reinterpret_cast<HANDLE>(-1);

constexpr DWORD kStdInputHandle  = static_cast<DWORD>(-10);
constexpr DWORD kStdOutputHandle = static_cast<DWORD>(-11);
constexpr DWORD kStdErrorHandle  = static_cast<DWORD>(-12);

constexpr DWORD kErrorInvalidHandle    = 6;
constexpr DWORD kErrorInvalidParameter = 87;
constexpr DWORD kErrorWriteFault       = 29;
constexpr DWORD kErrorReadFault        = 30;

}
