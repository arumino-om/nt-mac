#pragma once
/* windows.h の kernel32 の宣言から dllimport を外し，ここで定義できるようにする */
#define _KERNEL32_
#include <windows.h>
#include "ntdll/ntdll.h"
#include "shared/ntstatus.h"

/* ANSI / OEM コードページ．nt-mac では UTF-8 とする (Windows 10 の「UTF-8 を使用」と同じ) */
#define NTMAC_ANSI_CODE_PAGE CP_UTF8

/* NTSTATUS を Win32 のエラーに変換して LastError に設定し，成否を BOOL で返す */
static inline BOOL set_ntstatus(NTSTATUS status) {
    if (NT_SUCCESS(status)) return TRUE;
    RtlSetLastWin32Error(RtlNtStatusToDosError(status));
    return FALSE;
}

/* ミリ秒 (INFINITE あり) を NT の相対タイムアウトに変換する．INFINITE なら NULL を返す */
static inline PLARGE_INTEGER ms_to_timeout(DWORD ms, PLARGE_INTEGER buf) {
    if (ms == INFINITE) return NULL;
    buf->QuadPart = -(LONGLONG)ms * 10000;
    return buf;
}

static inline void init_unicode_string(UNICODE_STRING* us, LPCWSTR s) {
    size_t len = s ? wcslen(s) : 0;
    us->Length = (uint16_t)(len * sizeof(WCHAR));
    us->MaximumLength = us->Length + sizeof(WCHAR);
    us->_pad = 0;
    us->Buffer = (uint16_t*)s;
}

/* codepage.c: ANSI (UTF-8) の終端付き文字列をワイド文字に変換する．入り切らなければ FALSE */
BOOL ansi_to_wide(LPCSTR src, LPWSTR dst, int dst_len);
