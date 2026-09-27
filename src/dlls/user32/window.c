#include "user32_private.h"

static BOOL get_rect(uint32_t code, HWND hwnd, LPRECT rect) {
    ntmachost_rect_args args = {.Window = HANDLE_VALUE(hwnd)};
    if (!user_call(code, &args)) return FALSE;
    rect->left = args.Rect.left;
    rect->top = args.Rect.top;
    rect->right = args.Rect.right;
    rect->bottom = args.Rect.bottom;
    return TRUE;
}

BOOL WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect) {
    return get_rect(NTMACHOST_USER_GET_WINDOW_RECT, hWnd, lpRect);
}

BOOL WINAPI GetClientRect(HWND hWnd, LPRECT lpRect) {
    return get_rect(NTMACHOST_USER_GET_CLIENT_RECT, hWnd, lpRect);
}

BOOL WINAPI AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle) {
    (void)bMenu;     /* メニューは画面上部のメニューバーに出すので，ウィンドウの大きさに含めない */
    (void)dwExStyle;
    if ((dwStyle & WS_CAPTION) == WS_CAPTION) lpRect->top -= NTMAC_CAPTION_HEIGHT;
    return TRUE;
}

BOOL WINAPI AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu) {
    return AdjustWindowRectEx(lpRect, dwStyle, bMenu, 0);
}

/* ---- ウィンドウの属性 ---- */

static LONG_PTR get_long(HWND hwnd, int index, uint32_t size) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hwnd), .Index = index, .Size = size};
    return user_call(NTMACHOST_USER_GET_WINDOW_LONG, &args) ? (LONG_PTR)args.Value : 0;
}

static LONG_PTR set_long(HWND hwnd, int index, LONG_PTR value, uint32_t size) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hwnd), .Index = index, .Size = size, .Value = value};
    return user_call(NTMACHOST_USER_SET_WINDOW_LONG, &args) ? (LONG_PTR)args.Value : 0;
}

LONG_PTR WINAPI GetWindowLongPtrW(HWND hWnd, int nIndex) {
    return get_long(hWnd, nIndex, sizeof(LONG_PTR));
}

LONG_PTR WINAPI SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    return set_long(hWnd, nIndex, dwNewLong, sizeof(LONG_PTR));
}

LONG WINAPI GetWindowLongW(HWND hWnd, int nIndex) {
    return (LONG)get_long(hWnd, nIndex, sizeof(LONG));
}

LONG WINAPI SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) {
    return (LONG)set_long(hWnd, nIndex, dwNewLong, sizeof(LONG));
}

BOOL WINAPI IsWindow(HWND hWnd) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd)};
    ntmachost_call(NTMACHOST_USER_IS_WINDOW, &args);
    return args.Result != 0;
}

BOOL WINAPI IsWindowVisible(HWND hWnd) {
    return (GetWindowLongW(hWnd, GWL_STYLE) & WS_VISIBLE) != 0;
}

HWND WINAPI GetParent(HWND hWnd) {
    if (GetWindowLongW(hWnd, GWL_STYLE) & WS_CHILD) return (HWND)GetWindowLongPtrW(hWnd, GWLP_HWNDPARENT);
    return NULL; /* 所有者 (owner) は未実装 */
}

int WINAPI GetDlgCtrlID(HWND hWnd) {
    return (int)GetWindowLongPtrW(hWnd, GWLP_ID);
}

/* ---- テキスト ---- */

int WINAPI GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) {
    if (nMaxCount <= 0) return 0;
    return (int)SendMessageW(hWnd, WM_GETTEXT, (WPARAM)nMaxCount, (LPARAM)lpString);
}

int WINAPI GetWindowTextLengthW(HWND hWnd) {
    return (int)SendMessageW(hWnd, WM_GETTEXTLENGTH, 0, 0);
}

BOOL WINAPI SetWindowTextW(HWND hWnd, LPCWSTR lpString) {
    return (BOOL)SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)lpString);
}

/* DefWindowProc から使う (ホストのウィンドウのテキストを直接読み書きする) */
int internal_get_text(HWND hwnd, LPWSTR buf, int max) {
    ntmachost_text_args args = {.Window = HANDLE_VALUE(hwnd), .Buffer = (uint16_t*)buf, .Length = (uint32_t)max};
    return user_call(NTMACHOST_USER_GET_WINDOW_TEXT, &args) ? (int)args.Length : 0;
}

BOOL internal_set_text(HWND hwnd, LPCWSTR text) {
    static const WCHAR empty[] = {0};
    ntmachost_text_args args = {.Window = HANDLE_VALUE(hwnd), .Buffer = (uint16_t*)(text ? text : empty)};
    return user_call(NTMACHOST_USER_SET_WINDOW_TEXT, &args);
}

/* ---- メニュー ---- */

BOOL WINAPI SetMenu(HWND hWnd, HMENU hMenu) {
    ntmachost_menu_args args = {.Window = HANDLE_VALUE(hWnd), .Menu = HANDLE_VALUE(hMenu)};
    return user_call(NTMACHOST_USER_SET_MENU, &args);
}

HMENU WINAPI GetMenu(HWND hWnd) {
    ntmachost_menu_args args = {.Window = HANDLE_VALUE(hWnd)};
    return user_call(NTMACHOST_USER_GET_MENU, &args) ? (HMENU)(ULONG_PTR)args.Menu : NULL;
}

BOOL WINAPI DrawMenuBar(HWND hWnd) {
    return SetMenu(hWnd, GetMenu(hWnd)); /* メニューバーを作り直す */
}

/* ---- 作成・破棄・表示 ---- */

HWND WINAPI CreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y,
                            int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
                            LPVOID lpParam) {
    /* トップレベルでメニューの指定が無ければ，クラスのメニューを使う */
    if (!(dwStyle & WS_CHILD) && !hMenu) {
        WNDCLASSEXW wc = {.cbSize = sizeof(wc)};
        if (GetClassInfoExW(hInstance, lpClassName, &wc) && wc.lpszMenuName) {
            hMenu = LoadMenuW(wc.hInstance, wc.lpszMenuName);
        }
    }

    ntmachost_create_window_args args = {
        .ExStyle = dwExStyle,
        .Style = dwStyle & ~WS_VISIBLE, /* 表示は WM_CREATE の後に ShowWindow で行う */
        .ClassName = (const uint16_t*)lpClassName,
        /* アイコンの STATIC などでは名前が整数 ID (リソース名)．それは CREATESTRUCT で渡すだけにする */
        .WindowName = IS_INTRESOURCE(lpWindowName) ? NULL : (const uint16_t*)lpWindowName,
        .X = X, .Y = Y, .Width = nWidth, .Height = nHeight,
        .Parent = HANDLE_VALUE(hWndParent),
        .Menu = HANDLE_VALUE(hMenu),
        .Instance = HANDLE_VALUE(hInstance),
    };
    if (!NT_SUCCESS(ntmachost_call(NTMACHOST_USER_CREATE_WINDOW, &args))) {
        RtlSetLastWin32Error(ERROR_CANNOT_FIND_WND_CLASS);
        return NULL;
    }
    HWND hwnd = (HWND)(ULONG_PTR)args.Window;

    RECT rect, parent_client = {0};
    GetWindowRect(hwnd, &rect);
    if ((dwStyle & WS_CHILD) && hWndParent) {
        /* 子ウィンドウの位置は親のクライアント座標で渡す */
        RECT pr;
        GetWindowRect(hWndParent, &pr);
        GetClientRect(hWndParent, &parent_client);
        int caption = (pr.bottom - pr.top) - (parent_client.bottom - parent_client.top);
        OffsetRect(&rect, -pr.left, -(pr.top + caption));
    }

    CREATESTRUCTW cs = {
        .lpCreateParams = lpParam,
        .hInstance = hInstance,
        .hMenu = hMenu,
        .hwndParent = hWndParent,
        .cy = rect.bottom - rect.top,
        .cx = rect.right - rect.left,
        .y = rect.top,
        .x = rect.left,
        .style = (LONG)dwStyle,
        .lpszName = lpWindowName,
        .lpszClass = lpClassName,
        .dwExStyle = dwExStyle,
    };

    if (!SendMessageW(hwnd, WM_NCCREATE, 0, (LPARAM)&cs)) {
        DestroyWindow(hwnd);
        return NULL;
    }
    if (SendMessageW(hwnd, WM_CREATE, 0, (LPARAM)&cs) == -1) {
        DestroyWindow(hwnd);
        return NULL;
    }

    RECT client;
    GetClientRect(hwnd, &client);
    SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
    SendMessageW(hwnd, WM_MOVE, 0, MAKELPARAM(rect.left, rect.top));

    if (dwStyle & WS_VISIBLE) ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}

BOOL WINAPI DestroyWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) {
        RtlSetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (IsWindowVisible(hWnd)) ShowWindow(hWnd, SW_HIDE);
    SendMessageW(hWnd, WM_DESTROY, 0, 0);
    SendMessageW(hWnd, WM_NCDESTROY, 0, 0);
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd)};
    return user_call(NTMACHOST_USER_DESTROY_WINDOW, &args);
}

BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow) {
    BOOL show = nCmdShow != SW_HIDE;
    BOOL visible = IsWindowVisible(hWnd);
    if (show != visible) SendMessageW(hWnd, WM_SHOWWINDOW, show, 0);

    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd), .Index = nCmdShow};
    if (!user_call(NTMACHOST_USER_SHOW_WINDOW, &args)) return FALSE;
    return args.Result != 0;
}

BOOL WINAPI UpdateWindow(HWND hWnd) {
    ntmachost_rect_args args = {.Window = HANDLE_VALUE(hWnd)};
    if (!user_call(NTMACHOST_USER_GET_UPDATE_RECT, &args)) return FALSE;
    if (args.Result) SendMessageW(hWnd, WM_PAINT, 0, 0);
    return TRUE;
}

/* ---- 関係・状態 ---- */

HWND WINAPI GetWindow(HWND hWnd, UINT uCmd) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd), .Index = (int32_t)uCmd};
    return user_call(NTMACHOST_USER_GET_RELATED_WINDOW, &args) ? (HWND)(ULONG_PTR)args.Value : NULL;
}

BOOL WINAPI IsChild(HWND hWndParent, HWND hWnd) {
    for (HWND p = GetParent(hWnd); p; p = GetParent(p)) {
        if (p == hWndParent) return TRUE;
    }
    return FALSE;
}

BOOL WINAPI IsWindowEnabled(HWND hWnd) {
    return !(GetWindowLongW(hWnd, GWL_STYLE) & WS_DISABLED);
}

BOOL WINAPI EnableWindow(HWND hWnd, BOOL bEnable) {
    LONG style = GetWindowLongW(hWnd, GWL_STYLE);
    BOOL was_disabled = (style & WS_DISABLED) != 0;
    if (was_disabled == !bEnable) return was_disabled;
    SetWindowLongW(hWnd, GWL_STYLE, bEnable ? (style & ~WS_DISABLED) : (style | WS_DISABLED));
    SendMessageW(hWnd, WM_ENABLE, bEnable, 0);
    InvalidateRect(hWnd, NULL, TRUE);
    return was_disabled;
}

HWND WINAPI GetFocus(void) {
    ntmachost_window_args args = {0};
    ntmachost_call(NTMACHOST_USER_GET_FOCUS, &args);
    return (HWND)(ULONG_PTR)args.Value;
}

HWND WINAPI SetFocus(HWND hWnd) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd)};
    if (!user_call(NTMACHOST_USER_SET_FOCUS, &args)) return NULL;
    HWND old = (HWND)(ULONG_PTR)args.Value;
    if (old != hWnd) {
        if (old && IsWindow(old)) SendMessageW(old, WM_KILLFOCUS, (WPARAM)hWnd, 0);
        if (hWnd) SendMessageW(hWnd, WM_SETFOCUS, (WPARAM)old, 0);
    }
    return old;
}

HWND WINAPI GetCapture(void) {
    ntmachost_window_args args = {0};
    ntmachost_call(NTMACHOST_USER_GET_CAPTURE, &args);
    return (HWND)(ULONG_PTR)args.Value;
}

HWND WINAPI SetCapture(HWND hWnd) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd)};
    return user_call(NTMACHOST_USER_SET_CAPTURE, &args) ? (HWND)(ULONG_PTR)args.Value : NULL;
}

BOOL WINAPI ReleaseCapture(void) {
    HWND old = GetCapture();
    ntmachost_window_args args = {0};
    ntmachost_call(NTMACHOST_USER_SET_CAPTURE, &args);
    if (old) SendMessageW(old, WM_CAPTURECHANGED, 0, 0);
    return TRUE;
}

HWND WINAPI GetActiveWindow(void) {
    HWND w = GetFocus();
    while (w && (GetWindowLongW(w, GWL_STYLE) & WS_CHILD)) w = GetParent(w);
    return w;
}
