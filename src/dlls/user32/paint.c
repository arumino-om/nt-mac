#include "user32_private.h"

HDC WINAPI BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint) {
    ntmachost_paint_args args = {.Window = HANDLE_VALUE(hWnd)};
    if (!user_call(NTMACHOST_USER_BEGIN_PAINT, &args)) return NULL;

    memset(lpPaint, 0, sizeof(*lpPaint));
    lpPaint->hdc = (HDC)(ULONG_PTR)args.Dc;
    lpPaint->rcPaint.left = args.Paint.left;
    lpPaint->rcPaint.top = args.Paint.top;
    lpPaint->rcPaint.right = args.Paint.right;
    lpPaint->rcPaint.bottom = args.Paint.bottom;
    /* 背景の消去が必要なら WM_ERASEBKGND を送る．処理されなければ fErase を TRUE にして描画側に任せる */
    lpPaint->fErase = args.Erase ? !SendMessageW(hWnd, WM_ERASEBKGND, (WPARAM)lpPaint->hdc, 0) : FALSE;
    return lpPaint->hdc;
}

BOOL WINAPI EndPaint(HWND hWnd, const PAINTSTRUCT* lpPaint) {
    ntmachost_paint_args args = {.Window = HANDLE_VALUE(hWnd), .Dc = HANDLE_VALUE(lpPaint->hdc)};
    return user_call(NTMACHOST_USER_END_PAINT, &args);
}

HDC WINAPI GetDC(HWND hWnd) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd)};
    return user_call(NTMACHOST_USER_GET_DC, &args) ? (HDC)(ULONG_PTR)args.Value : NULL;
}

int WINAPI ReleaseDC(HWND hWnd, HDC hDC) {
    ntmachost_window_args args = {.Window = HANDLE_VALUE(hWnd), .Value = (int64_t)HANDLE_VALUE(hDC)};
    return user_call(NTMACHOST_USER_RELEASE_DC, &args);
}

static BOOL update_rect(uint32_t code, HWND hwnd, const RECT* rect, BOOL erase) {
    ntmachost_rect_args args = {.Window = HANDLE_VALUE(hwnd), .Flags = erase ? NTMAC_RECT_ERASE : 0};
    if (rect) {
        args.Rect = (ntmac_rect){rect->left, rect->top, rect->right, rect->bottom};
    } else {
        args.Flags |= NTMAC_RECT_WHOLE;
    }
    return user_call(code, &args);
}

BOOL WINAPI InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase) {
    return update_rect(NTMACHOST_USER_INVALIDATE_RECT, hWnd, lpRect, bErase);
}

BOOL WINAPI ValidateRect(HWND hWnd, const RECT* lpRect) {
    return update_rect(NTMACHOST_USER_VALIDATE_RECT, hWnd, lpRect, FALSE);
}

BOOL WINAPI GetUpdateRect(HWND hWnd, LPRECT lpRect, BOOL bErase) {
    (void)bErase;
    ntmachost_rect_args args = {.Window = HANDLE_VALUE(hWnd)};
    if (!user_call(NTMACHOST_USER_GET_UPDATE_RECT, &args)) return FALSE;
    if (lpRect) SetRect(lpRect, args.Rect.left, args.Rect.top, args.Rect.right, args.Rect.bottom);
    return args.Result != 0;
}

int WINAPI FillRect(HDC hDC, const RECT* lprc, HBRUSH hbr) {
    COLORREF color;
    if (!brush_color(hbr, &color)) return 0;
    ntmachost_fill_args args = {
        .Dc = HANDLE_VALUE(hDC),
        .Rect = {lprc->left, lprc->top, lprc->right, lprc->bottom},
        .Color = color,
    };
    return user_call(NTMACHOST_GDI_FILL_RECT, &args);
}
