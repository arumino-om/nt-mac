#pragma once
/* windows.h の kernel32 の宣言から dllimport を外し，ここで定義できるようにする */
#define _KERNEL32_
#include <windows.h>
#include "ntdll/ntdll.h"
#include "shared/ntstatus.h"

/* NTSTATUS を Win32 のエラーに変換して LastError に設定し，成否を BOOL で返す */
static inline BOOL set_ntstatus(NTSTATUS status) {
    if (NT_SUCCESS(status)) return TRUE;
    RtlSetLastWin32Error(RtlNtStatusToDosError(status));
    return FALSE;
}
