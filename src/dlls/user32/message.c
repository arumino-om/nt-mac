#include "user32_private.h"

int internal_get_text(HWND hwnd, LPWSTR buf, int max);
BOOL internal_set_text(HWND hwnd, LPCWSTR text);

LRESULT call_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    WNDPROC proc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    return proc ? proc(hwnd, msg, wparam, lparam) : 0;
}

static void to_msg(const ntmachost_msg* m, LPMSG msg) {
    msg->hwnd = (HWND)(ULONG_PTR)m->Window;
    msg->message = m->Message;
    msg->wParam = (WPARAM)m->WParam;
    msg->lParam = (LPARAM)m->LParam;
    msg->time = m->Time;
    msg->pt.x = m->PtX;
    msg->pt.y = m->PtY;
}

/*
 * メッセージを1つ取り出す．ホストから送られたメッセージ (WM_SIZE などの通知) は，ここでウィンドウプロシージャを呼んで処理し，
 * アプリには返さない．戻り値は NTMAC_MSG_*
 */
static uint32_t get_message(LPMSG msg, HWND hwnd, UINT min, UINT max, uint32_t flags) {
    for (;;) {
        ntmachost_msg_args args = {
            .FilterWindow = HANDLE_VALUE(hwnd),
            .FilterMin = min,
            .FilterMax = max,
            .Flags = flags,
        };
        if (!NT_SUCCESS(ntmachost_call(NTMACHOST_USER_GET_MESSAGE, &args))) return NTMAC_MSG_NONE;
        if (args.Result == NTMAC_MSG_SENT) {
            call_window_proc((HWND)(ULONG_PTR)args.Msg.Window, args.Msg.Message, (WPARAM)args.Msg.WParam,
                             (LPARAM)args.Msg.LParam);
            continue;
        }
        if (args.Result != NTMAC_MSG_NONE) to_msg(&args.Msg, msg);
        return args.Result;
    }
}

BOOL WINAPI GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    uint32_t r = get_message(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, NTMAC_GET_REMOVE | NTMAC_GET_WAIT);
    return r != NTMAC_MSG_QUIT;
}

BOOL WINAPI PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    uint32_t r = get_message(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, (wRemoveMsg & PM_REMOVE) ? NTMAC_GET_REMOVE : 0);
    return r != NTMAC_MSG_NONE;
}

BOOL WINAPI WaitMessage(void) {
    MSG msg;
    get_message(&msg, NULL, 0, 0, NTMAC_GET_WAIT); /* 取り出さずに待つ */
    return TRUE;
}

LRESULT WINAPI DispatchMessageW(const MSG* lpMsg) {
    if (lpMsg->message == WM_TIMER && lpMsg->lParam) {
        ((TIMERPROC)lpMsg->lParam)(lpMsg->hwnd, WM_TIMER, (UINT_PTR)lpMsg->wParam, lpMsg->time);
        return 0;
    }
    if (!lpMsg->hwnd) return 0;
    return call_window_proc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
}

/* 文字のメッセージ (WM_CHAR) は，今はホストがキー入力と同時に送っている */
BOOL WINAPI TranslateMessage(const MSG* lpMsg) {
    (void)lpMsg;
    return FALSE;
}

LRESULT WINAPI SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    /* 他のスレッドのウィンドウへの送信は未対応 (呼び出したスレッドでそのまま処理する) */
    return call_window_proc(hWnd, Msg, wParam, lParam);
}

LRESULT WINAPI CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    return lpPrevWndFunc ? lpPrevWndFunc(hWnd, Msg, wParam, lParam) : 0;
}

BOOL WINAPI PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    ntmachost_msg_args args = {
        .Msg = {.Window = HANDLE_VALUE(hWnd), .Message = Msg, .WParam = wParam, .LParam = lParam},
    };
    return user_call(NTMACHOST_USER_POST_MESSAGE, &args);
}

void WINAPI PostQuitMessage(int nExitCode) {
    ntmachost_window_args args = {.Value = nExitCode};
    ntmachost_call(NTMACHOST_USER_POST_QUIT_MESSAGE, &args);
}

LRESULT WINAPI DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    switch (Msg) {
    case WM_NCCREATE:
        return TRUE; /* ウィンドウのテキストは作成時にホストが設定済み */

    case WM_GETTEXT:
        return internal_get_text(hWnd, (LPWSTR)lParam, (int)wParam);

    case WM_GETTEXTLENGTH:
        return internal_get_text(hWnd, NULL, 0);

    case WM_SETTEXT:
        return internal_set_text(hWnd, (LPCWSTR)lParam);

    case WM_ERASEBKGND: {
        HBRUSH brush = (HBRUSH)GetClassLongPtrW(hWnd, GCLP_HBRBACKGROUND);
        if (!brush) return 0;
        RECT rect;
        GetClientRect(hWnd, &rect);
        FillRect((HDC)wParam, &rect, brush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE) SendMessageW(hWnd, WM_CLOSE, 0, 0);
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLORSCROLLBAR:
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

    default:
        return 0;
    }
}

UINT_PTR WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) {
    ntmachost_timer_args args = {
        .Window = HANDLE_VALUE(hWnd), .Id = nIDEvent, .Interval = uElapse, .Proc = (void*)lpTimerFunc,
    };
    if (!user_call(NTMACHOST_USER_SET_TIMER, &args)) return 0;
    return hWnd ? (nIDEvent ? nIDEvent : 1) : (UINT_PTR)args.Id;
}

BOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent) {
    ntmachost_timer_args args = {.Window = HANDLE_VALUE(hWnd), .Id = uIDEvent};
    return user_call(NTMACHOST_USER_KILL_TIMER, &args);
}
