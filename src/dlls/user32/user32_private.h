#pragma once
/* winuser.h の user32 の宣言から dllimport を外し，ここで定義できるようにする */
#define _USER32_
#include <windows.h>
#include "ntdll/ntdll.h"
#include "shared/ntmachost_user.h"

/* gdi32 (nt-mac 独自のエクスポート) */
__declspec(dllimport) BOOL WINAPI ntmac_GetBrushColor(HBRUSH brush, COLORREF* color);

/* NTSTATUS を Win32 のエラーに変換して LastError に設定し，成否を BOOL で返す */
static inline BOOL set_ntstatus(NTSTATUS status) {
    if (NT_SUCCESS(status)) return TRUE;
    RtlSetLastWin32Error(RtlNtStatusToDosError(status));
    return FALSE;
}

/* ホストのウィンドウ管理を呼ぶ */
static inline BOOL user_call(uint32_t code, void* args) {
    return set_ntstatus(ntmachost_call(code, args));
}

#define HANDLE_VALUE(h) ((uint64_t)(ULONG_PTR)(h))

/* message.c */
LRESULT call_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

/* sysparams.c: ブラシ (システムカラーの (HBRUSH)(色番号 + 1) を含む) の色 */
BOOL brush_color(HBRUSH brush, COLORREF* color);

/* sysparams.c: タイトルバーの高さ (NSWindow と同じ) */
#define NTMAC_CAPTION_HEIGHT 28
