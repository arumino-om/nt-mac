#include "user32_private.h"

static ATOM register_class(const WNDCLASSEXW* wc) {
    if (!wc || !wc->lpszClassName || !wc->lpfnWndProc) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ntmachost_class_args args = {
        .Info = {
            .Style = wc->style,
            .ClassExtra = wc->cbClsExtra,
            .WindowExtra = wc->cbWndExtra,
            .WndProc = (void*)wc->lpfnWndProc,
            .Instance = HANDLE_VALUE(wc->hInstance),
            .Icon = HANDLE_VALUE(wc->hIcon),
            .Cursor = HANDLE_VALUE(wc->hCursor),
            .Background = HANDLE_VALUE(wc->hbrBackground),
            .IconSmall = HANDLE_VALUE(wc->hIconSm),
            .MenuName = (const uint16_t*)wc->lpszMenuName,
            .ClassName = (const uint16_t*)wc->lpszClassName,
        },
    };
    if (!NT_SUCCESS(ntmachost_call(NTMACHOST_USER_REGISTER_CLASS, &args))) {
        RtlSetLastWin32Error(ERROR_CLASS_ALREADY_EXISTS);
        return 0;
    }
    return (ATOM)args.Atom;
}

ATOM WINAPI RegisterClassExW(const WNDCLASSEXW* lpwcx) {
    if (!lpwcx || lpwcx->cbSize != sizeof(WNDCLASSEXW)) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return register_class(lpwcx);
}

ATOM WINAPI RegisterClassW(const WNDCLASSW* lpWndClass) {
    WNDCLASSEXW wc = {
        .cbSize = sizeof(wc),
        .style = lpWndClass->style,
        .lpfnWndProc = lpWndClass->lpfnWndProc,
        .cbClsExtra = lpWndClass->cbClsExtra,
        .cbWndExtra = lpWndClass->cbWndExtra,
        .hInstance = lpWndClass->hInstance,
        .hIcon = lpWndClass->hIcon,
        .hCursor = lpWndClass->hCursor,
        .hbrBackground = lpWndClass->hbrBackground,
        .lpszMenuName = lpWndClass->lpszMenuName,
        .lpszClassName = lpWndClass->lpszClassName,
    };
    return register_class(&wc);
}

BOOL WINAPI GetClassInfoExW(HINSTANCE hInstance, LPCWSTR lpszClass, LPWNDCLASSEXW lpwcx) {
    (void)hInstance;
    ntmachost_class_args args = {.Info = {.ClassName = (const uint16_t*)lpszClass}};
    if (!NT_SUCCESS(ntmachost_call(NTMACHOST_USER_GET_CLASS_INFO, &args))) {
        RtlSetLastWin32Error(ERROR_CLASS_DOES_NOT_EXIST);
        return FALSE;
    }
    lpwcx->style = args.Info.Style;
    lpwcx->lpfnWndProc = (WNDPROC)args.Info.WndProc;
    lpwcx->cbClsExtra = args.Info.ClassExtra;
    lpwcx->cbWndExtra = args.Info.WindowExtra;
    lpwcx->hInstance = (HINSTANCE)(ULONG_PTR)args.Info.Instance;
    lpwcx->hIcon = (HICON)(ULONG_PTR)args.Info.Icon;
    lpwcx->hCursor = (HCURSOR)(ULONG_PTR)args.Info.Cursor;
    lpwcx->hbrBackground = (HBRUSH)(ULONG_PTR)args.Info.Background;
    lpwcx->lpszMenuName = (LPCWSTR)args.Info.MenuName;
    lpwcx->lpszClassName = lpszClass;
    lpwcx->hIconSm = (HICON)(ULONG_PTR)args.Info.IconSmall;
    return (ATOM)args.Atom;
}

BOOL WINAPI UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance) {
    (void)lpClassName;
    (void)hInstance;
    return TRUE; /* クラスの登録解除は未実装 (登録したままにする) */
}

ULONG_PTR WINAPI GetClassLongPtrW(HWND hWnd, int nIndex) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd), .Index = nIndex};
    return user_call(NTMACHOST_USER_GET_CLASS_LONG, &args) ? (ULONG_PTR)args.Value : 0;
}

DWORD WINAPI GetClassLongW(HWND hWnd, int nIndex) {
    return (DWORD)GetClassLongPtrW(hWnd, nIndex);
}
