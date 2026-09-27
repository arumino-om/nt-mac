#include "user32_private.h"

/*
 * 標準コントロール (STATIC と BUTTON)．見た目は Windows 10 のビジュアルスタイルに近づける．
 * ウィンドウの追加領域: [0] フォント，[8] 状態 (BUTTON) / アイコン (STATIC)
 */

#define EXTRA_FONT  0
#define EXTRA_STATE 8
#define CONTROL_EXTRA 16

/* BUTTON の状態 */
#define STATE_CHECK_MASK 0x3
#define STATE_PUSHED     0x4
#define STATE_FOCUSED    0x8

/* commctrl.h の分割ボタン (押しボタンとして描く) */
#ifndef BS_SPLITBUTTON
#define BS_SPLITBUTTON    0x0000000C
#define BS_DEFSPLITBUTTON 0x0000000D
#endif

static const WCHAR static_class[] = L"Static";
static const WCHAR button_class[] = L"Button";

static HFONT control_font(HWND hwnd) {
    return (HFONT)GetWindowLongPtrW(hwnd, EXTRA_FONT);
}

/* 親に WM_CTLCOLOR* を送り，背景のブラシを受け取る (文字色なども親が DC に設定する) */
static HBRUSH control_brush(HWND hwnd, HDC hdc, UINT msg) {
    HWND parent = GetParent(hwnd);
    HBRUSH brush = parent ? (HBRUSH)SendMessageW(parent, msg, (WPARAM)hdc, (LPARAM)hwnd) : NULL;
    if (!brush) {
        brush = (HBRUSH)DefWindowProcW(parent, msg, (WPARAM)hdc, (LPARAM)hwnd);
    }
    return brush;
}

static void notify_parent(HWND hwnd, UINT code) {
    HWND parent = GetParent(hwnd);
    if (parent) SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), code), (LPARAM)hwnd);
}

static LRESULT common_messages(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, BOOL* handled) {
    *handled = TRUE;
    switch (msg) {
    case WM_SETFONT:
        SetWindowLongPtrW(hwnd, EXTRA_FONT, (LONG_PTR)wp);
        if (LOWORD(lp)) InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)control_font(hwnd);
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
        InvalidateRect(hwnd, NULL, TRUE);
        return r;
    }
    case WM_ENABLE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1; /* 背景は WM_PAINT で塗る */
    }
    *handled = FALSE;
    return 0;
}

static int window_text(HWND hwnd, WCHAR* buf, int max) {
    return (int)SendMessageW(hwnd, WM_GETTEXT, (WPARAM)max, (LPARAM)buf);
}

/* ---- STATIC ---- */

static void paint_static(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    HBRUSH brush = control_brush(hwnd, hdc, WM_CTLCOLORSTATIC);
    UINT type = style & SS_TYPEMASK;

    if (type == SS_ETCHEDHORZ || type == SS_ETCHEDVERT || type == SS_ETCHEDFRAME) {
        DrawEdge(hdc, &rc, EDGE_ETCHED, type == SS_ETCHEDHORZ ? BF_TOP : type == SS_ETCHEDVERT ? BF_LEFT : BF_RECT);
        return;
    }
    if (type != SS_WHITERECT && type != SS_BLACKRECT && type != SS_GRAYRECT) FillRect(hdc, &rc, brush);
    switch (type) {
    case SS_ICON: {
        HICON icon = (HICON)GetWindowLongPtrW(hwnd, EXTRA_STATE);
        if (icon) DrawIconEx(hdc, 0, 0, icon, 0, 0, 0, NULL, DI_NORMAL);
        return;
    }
    case SS_BLACKRECT: FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOWFRAME)); return;
    case SS_GRAYRECT:  FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNSHADOW)); return;
    case SS_WHITERECT: FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW)); return;
    case SS_BLACKFRAME: FrameRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOWFRAME)); return;
    case SS_GRAYFRAME:  FrameRect(hdc, &rc, GetSysColorBrush(COLOR_BTNSHADOW)); return;
    case SS_WHITEFRAME: FrameRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW)); return;
    }

    WCHAR text[1024];
    int len = window_text(hwnd, text, 1024);
    UINT format = DT_EXPANDTABS | ((style & SS_NOPREFIX) ? DT_NOPREFIX : 0);
    switch (type) {
    case SS_CENTER: format |= DT_CENTER | DT_WORDBREAK; break;
    case SS_RIGHT:  format |= DT_RIGHT | DT_WORDBREAK; break;
    case SS_SIMPLE:
    case SS_LEFTNOWORDWRAP: format |= DT_SINGLELINE; break;
    default: format |= DT_WORDBREAK; break;
    }
    if (style & SS_CENTERIMAGE) format |= DT_SINGLELINE | DT_VCENTER;

    HGDIOBJ old = SelectObject(hdc, control_font(hwnd) ? (HGDIOBJ)control_font(hwnd) : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    if (!IsWindowEnabled(hwnd)) SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(hdc, text, len, &rc, format);
    SelectObject(hdc, old);
}

static LRESULT CALLBACK static_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    BOOL handled;
    LRESULT r = common_messages(hwnd, msg, wp, lp, &handled);
    if (handled) return r;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        if ((cs->style & SS_TYPEMASK) == SS_ICON && cs->lpszName) {
            /* アイコンの STATIC の名前はアイコンのリソース名 (ダイアログでは整数 ID) */
            HICON icon = LoadIconW(cs->hInstance, cs->lpszName);
            SetWindowLongPtrW(hwnd, EXTRA_STATE, (LONG_PTR)icon);
        }
        return 0;
    }
    case STM_SETICON: {
        LONG_PTR old = GetWindowLongPtrW(hwnd, EXTRA_STATE);
        SetWindowLongPtrW(hwnd, EXTRA_STATE, (LONG_PTR)wp);
        InvalidateRect(hwnd, NULL, TRUE);
        return old;
    }
    case STM_GETICON:
        return GetWindowLongPtrW(hwnd, EXTRA_STATE);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_static(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_STATIC;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- BUTTON ---- */

static UINT button_type(HWND hwnd) {
    return GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK;
}

static LONG_PTR button_state(HWND hwnd) {
    return GetWindowLongPtrW(hwnd, EXTRA_STATE);
}

static void set_button_state(HWND hwnd, LONG_PTR state) {
    if (state != button_state(hwnd)) {
        SetWindowLongPtrW(hwnd, EXTRA_STATE, state);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

static void fill_rgb(HDC hdc, const RECT* r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(hdc, r, b);
    DeleteObject(b);
}

static void frame_rgb(HDC hdc, const RECT* r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FrameRect(hdc, r, b);
    DeleteObject(b);
}

static void paint_button(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    UINT type = style & BS_TYPEMASK;
    LONG_PTR state = button_state(hwnd);
    BOOL enabled = IsWindowEnabled(hwnd);
    HBRUSH brush = control_brush(hwnd, hdc, WM_CTLCOLORBTN);

    WCHAR text[512];
    int len = window_text(hwnd, text, 512);
    HGDIOBJ old_font = SelectObject(hdc, control_font(hwnd) ? (HGDIOBJ)control_font(hwnd) : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, enabled ? GetSysColor(COLOR_BTNTEXT) : GetSysColor(COLOR_GRAYTEXT));

    switch (type) {
    case BS_PUSHBUTTON:
    case BS_DEFPUSHBUTTON:
    case BS_DEFSPLITBUTTON:
    case BS_SPLITBUTTON: {
        BOOL pushed = state & STATE_PUSHED;
        BOOL def = type == BS_DEFPUSHBUTTON || type == BS_DEFSPLITBUTTON;
        fill_rgb(hdc, &rc, !enabled ? RGB(204, 204, 204) : pushed ? RGB(204, 228, 247) : RGB(225, 225, 225));
        frame_rgb(hdc, &rc, !enabled ? RGB(191, 191, 191) : pushed ? RGB(0, 84, 153)
                                                           : def ? RGB(0, 120, 215) : RGB(173, 173, 173));
        if (def && enabled) {
            RECT inner = rc;
            InflateRect(&inner, -1, -1);
            frame_rgb(hdc, &inner, RGB(0, 120, 215));
        }
        DrawTextW(hdc, text, len, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        break;
    }
    case BS_CHECKBOX:
    case BS_AUTOCHECKBOX:
    case BS_3STATE:
    case BS_AUTO3STATE:
    case BS_RADIOBUTTON:
    case BS_AUTORADIOBUTTON: {
        FillRect(hdc, &rc, brush);
        BOOL radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
        int size = 13;
        RECT box = {rc.left, (rc.top + rc.bottom - size) / 2, rc.left + size, (rc.top + rc.bottom - size) / 2 + size};
        if (!radio) {
            fill_rgb(hdc, &box, enabled ? RGB(255, 255, 255) : RGB(230, 230, 230));
            frame_rgb(hdc, &box, (state & STATE_PUSHED) ? RGB(0, 84, 153) : RGB(51, 51, 51));
        }
        WCHAR mark[2] = {0};
        if (radio) mark[0] = (state & STATE_CHECK_MASK) ? 0x25C9 : 0x25CB; /* ◉ / ○ */
        else if ((state & STATE_CHECK_MASK) == BST_CHECKED) mark[0] = 0x2713; /* ✓ */
        else if ((state & STATE_CHECK_MASK) == BST_INDETERMINATE) mark[0] = 0x25A0; /* ■ */
        if (mark[0]) DrawTextW(hdc, mark, 1, &box, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOCLIP);

        RECT label = rc;
        label.left += size + 5;
        DrawTextW(hdc, text, len, &label, DT_SINGLELINE | DT_VCENTER);
        break;
    }
    case BS_GROUPBOX: {
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        RECT frame = rc;
        frame.top += tm.tmHeight / 2;
        frame_rgb(hdc, &frame, RGB(220, 220, 220));
        if (len) {
            SIZE extent;
            GetTextExtentPoint32W(hdc, text, len, &extent);
            RECT label = {rc.left + 8, rc.top, rc.left + 8 + extent.cx + 4, rc.top + tm.tmHeight};
            FillRect(hdc, &label, brush);
            label.left += 2;
            DrawTextW(hdc, text, len, &label, DT_SINGLELINE);
        }
        break;
    }
    default:
        FillRect(hdc, &rc, brush);
        break;
    }
    SelectObject(hdc, old_font);
}

static void click_button(HWND hwnd) {
    UINT type = button_type(hwnd);
    LONG_PTR state = button_state(hwnd);
    if (type == BS_AUTOCHECKBOX) {
        state = (state & ~STATE_CHECK_MASK) | ((state & STATE_CHECK_MASK) ? BST_UNCHECKED : BST_CHECKED);
    } else if (type == BS_AUTO3STATE) {
        state = (state & ~STATE_CHECK_MASK) | (((state & STATE_CHECK_MASK) + 1) % 3);
    } else if (type == BS_AUTORADIOBUTTON) {
        /* 同じグループ (WS_GROUP で区切られた範囲) の他のラジオボタンを外す */
        HWND parent = GetParent(hwnd);
        for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            if (c != hwnd && button_type(c) == BS_AUTORADIOBUTTON) SendMessageW(c, BM_SETCHECK, BST_UNCHECKED, 0);
        }
        state = (state & ~STATE_CHECK_MASK) | BST_CHECKED;
    }
    set_button_state(hwnd, state);
    notify_parent(hwnd, BN_CLICKED);
}

static LRESULT CALLBACK button_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    BOOL handled;
    LRESULT r = common_messages(hwnd, msg, wp, lp, &handled);
    if (handled) return r;

    LONG_PTR state = button_state(hwnd);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_button(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (button_type(hwnd) == BS_GROUPBOX) return 0;
        SetCapture(hwnd);
        SetFocus(hwnd);
        set_button_state(hwnd, state | STATE_PUSHED);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            set_button_state(hwnd, PtInRect(&rc, pt) ? (state | STATE_PUSHED) : (state & ~STATE_PUSHED));
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() != hwnd) return 0;
        ReleaseCapture();
        if (state & STATE_PUSHED) {
            set_button_state(hwnd, state & ~STATE_PUSHED);
            click_button(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_SPACE) set_button_state(hwnd, state | STATE_PUSHED);
        return 0;
    case WM_KEYUP:
        if (wp == VK_SPACE && (state & STATE_PUSHED)) {
            set_button_state(hwnd, state & ~STATE_PUSHED);
            click_button(hwnd);
        }
        return 0;
    case WM_SETFOCUS:
        set_button_state(hwnd, state | STATE_FOCUSED);
        return 0;
    case WM_KILLFOCUS:
        set_button_state(hwnd, state & ~(STATE_FOCUSED | STATE_PUSHED));
        return 0;
    case BM_CLICK:
        click_button(hwnd);
        return 0;
    case BM_GETCHECK:
        return state & STATE_CHECK_MASK;
    case BM_SETCHECK:
        set_button_state(hwnd, (state & ~STATE_CHECK_MASK) | (wp & STATE_CHECK_MASK));
        return 0;
    case BM_GETSTATE:
        return (state & STATE_CHECK_MASK) | ((state & STATE_PUSHED) ? BST_PUSHED : 0) |
               ((state & STATE_FOCUSED) ? BST_FOCUS : 0);
    case BM_SETSTATE:
        set_button_state(hwnd, wp ? (state | STATE_PUSHED) : (state & ~STATE_PUSHED));
        return 0;
    case BM_SETSTYLE: {
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        SetWindowLongW(hwnd, GWL_STYLE, (style & ~BS_TYPEMASK) | (wp & BS_TYPEMASK));
        if (LOWORD(lp)) InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_GETDLGCODE: {
        UINT type = button_type(hwnd);
        if (type == BS_DEFPUSHBUTTON) return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
        if (type == BS_PUSHBUTTON) return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
        if (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) return DLGC_BUTTON | DLGC_RADIOBUTTON;
        if (type == BS_GROUPBOX) return DLGC_STATIC;
        return DLGC_BUTTON;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 登録 ---- */

void register_controls(HINSTANCE user32) {
    WNDCLASSEXW wc = {
        .cbSize = sizeof(wc),
        .style = CS_GLOBALCLASS | CS_PARENTDC,
        .cbWndExtra = CONTROL_EXTRA,
        .hInstance = user32,
        .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
    };
    wc.lpfnWndProc = static_proc;
    wc.lpszClassName = static_class;
    RegisterClassExW(&wc);

    wc.style |= CS_DBLCLKS;
    wc.lpfnWndProc = button_proc;
    wc.lpszClassName = button_class;
    RegisterClassExW(&wc);
}
