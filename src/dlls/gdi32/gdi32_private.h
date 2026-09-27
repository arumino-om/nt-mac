#pragma once
/* wingdi.h の gdi32 の宣言から dllimport を外し，ここで定義できるようにする */
#define _GDI32_
#include <windows.h>
#include "ntdll/ntdll.h"
#include "shared/ntmachost_user.h"

#define HANDLE_VALUE(h) ((uint64_t)(ULONG_PTR)(h))

/* NTSTATUS を Win32 のエラーに変換して LastError に設定し，成否を BOOL で返す */
static inline BOOL gdi_call(uint32_t code, void* args) {
    NTSTATUS status = ntmachost_call(code, args);
    if (NT_SUCCESS(status)) return TRUE;
    RtlSetLastWin32Error(RtlNtStatusToDosError(status));
    return FALSE;
}

/* object.c */
BOOL font_logfont(HGDIOBJ font, LOGFONTW* lf);
HGDIOBJ stock_system_font(void);

/* nt-mac 独自: ブラシの色を返す (user32 の FillRect が使う) */
BOOL WINAPI ntmac_GetBrushColor(HBRUSH brush, COLORREF* color);
