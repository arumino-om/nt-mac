#include "user32_private.h"

static HMODULE module_or_exe(HINSTANCE inst) {
    return inst ? inst : GetModuleHandleW(NULL);
}

static const void* load_resource(HINSTANCE inst, LPCWSTR name, LPCWSTR type, DWORD* size) {
    HMODULE module = module_or_exe(inst);
    HRSRC res = FindResourceW(module, name, type);
    if (!res) return NULL;
    if (size) *size = SizeofResource(module, res);
    return LockResource(LoadResource(module, res));
}

/* ---- 文字列 ---- */

int WINAPI LoadStringW(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax) {
    /* 文字列は16個ずつのブロックに，長さ (WORD) と文字列の組で並んでいる */
    const WCHAR* p = load_resource(hInstance, MAKEINTRESOURCEW((uID >> 4) + 1), (LPCWSTR)RT_STRING, NULL);
    if (!p) return 0;
    for (UINT i = 0; i < (uID & 15); i++) p += *p + 1;
    int len = *p;

    if (cchBufferMax == 0) {
        /* バッファの代わりにリソースの文字列へのポインタを返す */
        *(LPCWSTR*)lpBuffer = p + 1;
        return len;
    }
    if (!lpBuffer) return 0;
    int n = min(len, cchBufferMax - 1);
    memcpy(lpBuffer, p + 1, (size_t)n * sizeof(WCHAR));
    lpBuffer[n] = 0;
    return n;
}

/* ---- カーソル ----
 * カーソルの形の変更はまだ無いので，NULL でない擬似的なハンドルを返すだけ (リソースの存在だけは確認する) */

static HANDLE pseudo_handle(LPCWSTR name, WORD kind) {
    ULONG_PTR id = IS_INTRESOURCE(name) ? (ULONG_PTR)name : 0x7FFF;
    return (HANDLE)(((ULONG_PTR)kind << 16) | (id & 0xFFFF));
}

HCURSOR WINAPI LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName) {
    if (hInstance && !load_resource(hInstance, lpCursorName, (LPCWSTR)RT_GROUP_CURSOR, NULL)) {
        RtlSetLastWin32Error(ERROR_RESOURCE_NAME_NOT_FOUND);
        return NULL;
    }
    return (HCURSOR)pseudo_handle(lpCursorName, 0x0C);
}

HCURSOR WINAPI SetCursor(HCURSOR hCursor) {
    (void)hCursor;
    return NULL; /* カーソルの形の変更は未実装 */
}

/* ---- アクセラレータ ----
 * HACCEL はリソースの表 (ACCELTABLEENTRY の配列．最後の要素は fFlags に 0x80) をそのまま指す */

typedef struct accel_entry {
    WORD fFlags;
    WORD wAnsi;
    WORD wId;
    WORD padding;
} accel_entry;

HACCEL WINAPI LoadAcceleratorsW(HINSTANCE hInstance, LPCWSTR lpTableName) {
    return (HACCEL)load_resource(hInstance, lpTableName, (LPCWSTR)RT_ACCELERATOR, NULL);
}

BOOL WINAPI DestroyAcceleratorTable(HACCEL hAccel) {
    (void)hAccel;
    return TRUE;
}

static BOOL key_down(int vk) {
    return (GetKeyState(vk) & 0x8000) != 0;
}

int WINAPI TranslateAcceleratorW(HWND hWnd, HACCEL hAccTable, LPMSG lpMsg) {
    if (!hWnd || !hAccTable || !lpMsg) return 0;
    UINT msg = lpMsg->message;
    BOOL is_key = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    BOOL is_char = msg == WM_CHAR || msg == WM_SYSCHAR;
    if (!is_key && !is_char) return 0;

    BYTE shift = key_down(VK_SHIFT), ctrl = key_down(VK_CONTROL), alt = key_down(VK_MENU) || msg == WM_SYSCHAR;
    for (const accel_entry* e = (const accel_entry*)hAccTable;; e++) {
        BOOL match;
        if (e->fFlags & FVIRTKEY) {
            match = is_key && e->wAnsi == lpMsg->wParam && !!(e->fFlags & FSHIFT) == shift &&
                    !!(e->fFlags & FCONTROL) == ctrl && !!(e->fFlags & FALT) == alt;
        } else {
            /* 文字のアクセラレータ．Alt 付きは WM_SYSCHAR に対応する */
            match = is_char && e->wAnsi == lpMsg->wParam && !!(e->fFlags & FALT) == (msg == WM_SYSCHAR);
        }
        if (match) {
            SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(e->wId, 1), 0);
            return 1;
        }
        if (e->fFlags & 0x80) break;
    }
    return 0;
}

/* ---- メニュー ---- */

static HMENU create_menu(BOOL popup) {
    ntmachost_menu_args args = {.Flags = popup ? NTMAC_MENU_POPUP : 0};
    return user_call(NTMACHOST_USER_CREATE_MENU, &args) ? (HMENU)(ULONG_PTR)args.Menu : NULL;
}

HMENU WINAPI CreateMenu(void) {
    return create_menu(FALSE);
}

HMENU WINAPI CreatePopupMenu(void) {
    return create_menu(TRUE);
}

BOOL WINAPI AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem) {
    BOOL is_string = !(uFlags & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW));
    ntmachost_menu_args args = {
        .Menu = HANDLE_VALUE(hMenu),
        .Flags = uFlags,
        .Item = uIDNewItem,
        .Text = is_string ? (const uint16_t*)lpNewItem : NULL,
    };
    return user_call(NTMACHOST_USER_APPEND_MENU, &args);
}

BOOL WINAPI DestroyMenu(HMENU hMenu) {
    ntmachost_menu_args args = {.Menu = HANDLE_VALUE(hMenu)};
    return user_call(NTMACHOST_USER_DESTROY_MENU, &args);
}

/* MENU (標準形式): 項目 = WORD flags，(ポップアップでなければ) WORD id，終端付きの文字列．MF_END が各階層の最後 */
static const WORD* parse_menu(HMENU menu, const WORD* p) {
    for (;;) {
        WORD flags = *p++;
        WORD id = 0;
        if (!(flags & MF_POPUP)) id = *p++;
        LPCWSTR text = (LPCWSTR)p;
        p += wcslen(text) + 1;

        if (flags & MF_POPUP) {
            HMENU sub = CreatePopupMenu();
            p = parse_menu(sub, p);
            AppendMenuW(menu, (flags & ~MF_END) | MF_POPUP, (UINT_PTR)sub, text);
        } else if (!id && !*text && !(flags & ~MF_END)) {
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        } else {
            AppendMenuW(menu, flags & ~MF_END, id, text);
        }
        if (flags & MF_END) return p;
    }
}

/* MENUEX: 項目 = DWORD type，DWORD state，DWORD id，WORD resinfo，文字列，4 バイト境界，(ポップアップなら DWORD helpid) */
static const BYTE* parse_menuex(HMENU menu, const BYTE* p) {
    for (;;) {
        DWORD type = *(const DWORD*)p;
        DWORD state = *(const DWORD*)(p + 4);
        DWORD id = *(const DWORD*)(p + 8);
        WORD resinfo = *(const WORD*)(p + 12);
        LPCWSTR text = (LPCWSTR)(p + 14);
        p = (const BYTE*)(text + wcslen(text) + 1);
        p = (const BYTE*)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);

        UINT flags = (type & (MFT_SEPARATOR | MFT_RADIOCHECK)) | (state & (MFS_GRAYED | MFS_CHECKED));
        if (resinfo & 0x01) {
            p += 4; /* helpid */
            HMENU sub = CreatePopupMenu();
            p = parse_menuex(sub, p);
            AppendMenuW(menu, flags | MF_POPUP, (UINT_PTR)sub, text);
        } else {
            AppendMenuW(menu, flags, id, (type & MFT_SEPARATOR) ? NULL : text);
        }
        if (resinfo & 0x80) return p;
    }
}

HMENU WINAPI LoadMenuW(HINSTANCE hInstance, LPCWSTR lpMenuName) {
    const WORD* p = load_resource(hInstance, lpMenuName, (LPCWSTR)RT_MENU, NULL);
    if (!p) return NULL;
    HMENU menu = CreateMenu();
    if (!menu) return NULL;
    if (p[0] == 1) {
        parse_menuex(menu, (const BYTE*)(p + 2) + p[1]); /* p[1] は項目の先頭までのオフセット */
    } else {
        parse_menu(menu, p + 2 + p[1] / sizeof(WORD));
    }
    return menu;
}
