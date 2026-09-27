#include "user32_private.h"

/*
 * ダイアログマネージャ．
 *   - テンプレート (DLGTEMPLATE / DLGTEMPLATEEX) からダイアログとコントロールを作る
 *   - ダイアログの単位はダイアログのフォントの平均文字幅と高さから求める
 *   - モーダルのループ (DialogBox) と，Enter / Esc / Tab のキー操作 (IsDialogMessage)
 */

void register_controls(HINSTANCE user32);

static const WCHAR dialog_class[] = L"#32770";

/* ダイアログごとの内部の情報．ウィンドウの追加領域は DWLP_* で使われるので，別に持つ */
typedef struct dialog_info {
    struct dialog_info* next;
    HWND hwnd;
    HFONT font;
    BOOL own_font;          /* テンプレートから作ったフォント (破棄時に削除する) */
    int base_x, base_y;     /* ダイアログの単位 (4 x 8) あたりのピクセル */
    BOOL ended;
    INT_PTR result;
    UINT default_id;
} dialog_info;

static dialog_info* dialogs;

static dialog_info* info_of(HWND hwnd) {
    for (dialog_info* d = dialogs; d; d = d->next) {
        if (d->hwnd == hwnd) return d;
    }
    return NULL;
}

static void free_info(HWND hwnd) {
    for (dialog_info** p = &dialogs; *p; p = &(*p)->next) {
        if ((*p)->hwnd == hwnd) {
            dialog_info* d = *p;
            *p = d->next;
            if (d->own_font) DeleteObject(d->font);
            HeapFree(GetProcessHeap(), 0, d);
            return;
        }
    }
}

/* ---- テンプレートの解析 ---- */

typedef struct dialog_template {
    DWORD style, ex_style;
    int count;
    short x, y, cx, cy;
    LPCWSTR menu, class_name, caption;
    WORD point_size, weight;
    BOOL italic;
    LPCWSTR face;
    const BYTE* items;
    BOOL ex;
} dialog_template;

typedef struct item_template {
    DWORD style, ex_style, id;
    short x, y, cx, cy;
    LPCWSTR class_name, title;
    const void* data;       /* 作成時のデータ (無ければ NULL) */
} item_template;

/* 名前か序数 (0xFFFF + WORD) を読む．序数は MAKEINTRESOURCE にする */
static const WORD* read_name(const WORD* p, LPCWSTR* out) {
    if (*p == 0x0000) {
        *out = NULL;
        return p + 1;
    }
    if (*p == 0xFFFF) {
        *out = MAKEINTRESOURCEW(p[1]);
        return p + 2;
    }
    *out = (LPCWSTR)p;
    return p + wcslen((LPCWSTR)p) + 1;
}

static const BYTE* align4(const void* p) {
    return (const BYTE*)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
}

static void parse_template(const void* data, dialog_template* t) {
    const WORD* p = data;
    memset(t, 0, sizeof(*t));
    t->ex = p[0] == 1 && p[1] == 0xFFFF;
    if (t->ex) {
        const DWORD* d = (const DWORD*)(p + 2);  /* dlgVer, signature の後: helpID, exStyle, style */
        t->ex_style = d[1];
        t->style = d[2];
        p = (const WORD*)(d + 3);
    } else {
        const DWORD* d = (const DWORD*)p;         /* style, exStyle */
        t->style = d[0];
        t->ex_style = d[1];
        p = (const WORD*)(d + 2);
    }
    t->count = *p++;
    t->x = (short)*p++;
    t->y = (short)*p++;
    t->cx = (short)*p++;
    t->cy = (short)*p++;
    p = read_name(p, &t->menu);
    p = read_name(p, &t->class_name);
    p = read_name(p, &t->caption);
    if (t->style & DS_SETFONT) {
        t->point_size = *p++;
        if (t->ex) {
            t->weight = *p++;
            t->italic = *(const BYTE*)p;
            p++; /* italic, charset */
        }
        t->face = (LPCWSTR)p;
        p += wcslen(t->face) + 1;
    }
    t->items = align4(p);
}

static const BYTE* parse_item(const BYTE* data, BOOL ex, item_template* it) {
    const WORD* p;
    memset(it, 0, sizeof(*it));
    if (ex) {
        const DWORD* d = (const DWORD*)data; /* helpID, exStyle, style */
        it->ex_style = d[1];
        it->style = d[2];
        p = (const WORD*)(d + 3);
    } else {
        const DWORD* d = (const DWORD*)data; /* style, exStyle */
        it->style = d[0];
        it->ex_style = d[1];
        p = (const WORD*)(d + 2);
    }
    it->x = (short)*p++;
    it->y = (short)*p++;
    it->cx = (short)*p++;
    it->cy = (short)*p++;
    if (ex) {
        it->id = *(const DWORD*)p;
        p += 2;
    } else {
        it->id = *p++;
    }

    p = read_name(p, &it->class_name);
    if (IS_INTRESOURCE(it->class_name)) {
        static const WCHAR* builtin[] = {L"Button", L"Edit", L"Static", L"ListBox", L"ScrollBar", L"ComboBox"};
        WORD atom = (WORD)(ULONG_PTR)it->class_name;
        it->class_name = atom >= 0x80 && atom <= 0x85 ? builtin[atom - 0x80] : L"Static";
    }
    p = read_name(p, &it->title);

    WORD extra = *p++;
    it->data = extra ? (const void*)p : NULL;
    return align4((const BYTE*)p + extra);
}

/* ---- ダイアログの単位 ---- */

static void base_units(HFONT font, int* x, int* y) {
    HDC hdc = GetDC(NULL);
    HGDIOBJ old = SelectObject(hdc, font ? (HGDIOBJ)font : GetStockObject(SYSTEM_FONT));
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SIZE size;
    GetTextExtentPoint32W(hdc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &size);
    SelectObject(hdc, old);
    ReleaseDC(NULL, hdc);
    *x = (size.cx / 26 + 1) / 2;
    *y = tm.tmHeight;
}

static int map_x(const dialog_info* d, int v) {
    return MulDiv(v, d->base_x, 4);
}

static int map_y(const dialog_info* d, int v) {
    return MulDiv(v, d->base_y, 8);
}

BOOL WINAPI MapDialogRect(HWND hDlg, LPRECT lpRect) {
    dialog_info* d = info_of(hDlg);
    if (!d) return FALSE;
    lpRect->left = map_x(d, lpRect->left);
    lpRect->right = map_x(d, lpRect->right);
    lpRect->top = map_y(d, lpRect->top);
    lpRect->bottom = map_y(d, lpRect->bottom);
    return TRUE;
}

LONG WINAPI GetDialogBaseUnits(void) {
    int x, y;
    base_units(NULL, &x, &y);
    return MAKELONG(x, y);
}

/* ---- 作成 ---- */

static HWND first_tab_stop(HWND hDlg) {
    for (HWND c = GetWindow(hDlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        LONG style = GetWindowLongW(c, GWL_STYLE);
        if ((style & WS_TABSTOP) && (style & WS_VISIBLE) && !(style & WS_DISABLED)) return c;
    }
    return NULL;
}

HWND WINAPI CreateDialogIndirectParamW(HINSTANCE hInstance, LPCDLGTEMPLATEW lpTemplate, HWND hWndParent,
                                       DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    dialog_template t;
    parse_template(lpTemplate, &t);

    dialog_info* d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) return NULL;
    if (t.style & DS_SETFONT) {
        int height = t.point_size == 0x7FFF ? -12 : -MulDiv(t.point_size, 96, 72);
        d->font = CreateFontW(height, 0, 0, 0, t.weight ? t.weight : FW_NORMAL, t.italic, 0, 0, DEFAULT_CHARSET, 0, 0,
                              0, 0, t.face);
        d->own_font = TRUE;
    }
    base_units(d->font, &d->base_x, &d->base_y);
    d->default_id = IDOK;

    /* クライアント領域の大きさから，ウィンドウの大きさと位置を決める */
    DWORD style = (t.style & ~(WS_VISIBLE | DS_SETFONT | DS_MODALFRAME | DS_FIXEDSYS | DS_CENTER | DS_3DLOOK)) |
                  ((t.style & WS_CHILD) ? 0 : WS_POPUP);
    RECT rc = {0, 0, map_x(d, t.cx), map_y(d, t.cy)};
    AdjustWindowRectEx(&rc, style, FALSE, t.ex_style);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x, y;
    HWND owner = hWndParent;
    while (owner && (GetWindowLongW(owner, GWL_STYLE) & WS_CHILD)) owner = GetParent(owner);
    if (style & WS_CHILD) {
        x = map_x(d, t.x);
        y = map_y(d, t.y);
    } else if (owner) {
        /* 所有者の中央に置く */
        RECT orc;
        GetWindowRect(owner, &orc);
        x = orc.left + (orc.right - orc.left - w) / 2;
        y = orc.top + (orc.bottom - orc.top - h) / 3;
    } else {
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + (work.right - work.left - w) / 2;
        y = work.top + (work.bottom - work.top - h) / 3;
    }

    /* メッセージの処理が始まる前に情報を登録しておく (ハンドルは作成後に設定する) */
    d->next = dialogs;
    dialogs = d;
    HWND hDlg = CreateWindowExW(t.ex_style, t.class_name ? t.class_name : dialog_class, t.caption, style, x, y, w, h,
                                (style & WS_CHILD) ? hWndParent : owner,
                                t.menu ? LoadMenuW(hInstance, t.menu) : NULL, hInstance, NULL);
    if (!hDlg) {
        dialogs = d->next;
        if (d->own_font) DeleteObject(d->font);
        HeapFree(GetProcessHeap(), 0, d);
        return NULL;
    }
    d->hwnd = hDlg;
    SetWindowLongPtrW(hDlg, DWLP_DLGPROC, (LONG_PTR)lpDialogFunc);
    if (d->font) SendMessageW(hDlg, WM_SETFONT, (WPARAM)d->font, FALSE);

    /* コントロール */
    const BYTE* p = t.items;
    for (int i = 0; i < t.count; i++) {
        item_template it;
        p = parse_item(p, t.ex, &it);
        HWND ctrl = CreateWindowExW(it.ex_style | WS_EX_NOPARENTNOTIFY, it.class_name, it.title, it.style | WS_CHILD,
                                    map_x(d, it.x), map_y(d, it.y), map_x(d, it.cx), map_y(d, it.cy), hDlg,
                                    (HMENU)(ULONG_PTR)it.id, hInstance, (LPVOID)it.data);
        if (!ctrl) continue;
        if (d->font) SendMessageW(ctrl, WM_SETFONT, (WPARAM)d->font, FALSE);
        if ((SendMessageW(ctrl, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)) d->default_id = it.id;
    }

    /* WM_INITDIALOG が TRUE を返したら，最初のタブ位置のコントロールにフォーカスを置く */
    HWND focus = first_tab_stop(hDlg);
    if (lpDialogFunc && lpDialogFunc(hDlg, WM_INITDIALOG, (WPARAM)focus, dwInitParam) && focus) SetFocus(focus);

    if (t.style & WS_VISIBLE) {
        ShowWindow(hDlg, SW_SHOW);
        UpdateWindow(hDlg);
    }
    return hDlg;
}

static LPCDLGTEMPLATEW find_template(HINSTANCE inst, LPCWSTR name) {
    HMODULE module = inst ? inst : GetModuleHandleW(NULL);
    HRSRC res = FindResourceW(module, name, (LPCWSTR)RT_DIALOG);
    return res ? LockResource(LoadResource(module, res)) : NULL;
}

HWND WINAPI CreateDialogParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc,
                               LPARAM dwInitParam) {
    LPCDLGTEMPLATEW t = find_template(hInstance, lpTemplateName);
    return t ? CreateDialogIndirectParamW(hInstance, t, hWndParent, lpDialogFunc, dwInitParam) : NULL;
}

/* ---- モーダル ---- */

INT_PTR WINAPI DialogBoxIndirectParamW(HINSTANCE hInstance, LPCDLGTEMPLATEW hDialogTemplate, HWND hWndParent,
                                       DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    HWND owner = hWndParent;
    while (owner && (GetWindowLongW(owner, GWL_STYLE) & WS_CHILD)) owner = GetParent(owner);

    HWND hDlg = CreateDialogIndirectParamW(hInstance, hDialogTemplate, owner, lpDialogFunc, dwInitParam);
    if (!hDlg) return -1;
    dialog_info* d = info_of(hDlg);

    BOOL owner_was_disabled = owner ? EnableWindow(owner, FALSE) : TRUE;
    if (!d->ended) {
        ShowWindow(hDlg, SW_SHOW);
        UpdateWindow(hDlg);
    }

    MSG msg;
    while (!d->ended) {
        if (!GetMessageW(&msg, NULL, 0, 0)) {
            PostQuitMessage((int)msg.wParam); /* 外側のメッセージループにも終了を伝える */
            break;
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    INT_PTR result = d->ended ? d->result : -1;
    if (owner && !owner_was_disabled) EnableWindow(owner, TRUE);
    DestroyWindow(hDlg);
    if (owner) SetFocus(owner);
    return result;
}

INT_PTR WINAPI DialogBoxParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc,
                               LPARAM dwInitParam) {
    LPCDLGTEMPLATEW t = find_template(hInstance, lpTemplateName);
    if (!t) {
        RtlSetLastWin32Error(ERROR_RESOURCE_NAME_NOT_FOUND);
        return -1;
    }
    return DialogBoxIndirectParamW(hInstance, t, hWndParent, lpDialogFunc, dwInitParam);
}

BOOL WINAPI EndDialog(HWND hDlg, INT_PTR nResult) {
    dialog_info* d = info_of(hDlg);
    if (!d) {
        RtlSetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    d->ended = TRUE;
    d->result = nResult;
    ShowWindow(hDlg, SW_HIDE);
    PostMessageW(hDlg, WM_NULL, 0, 0); /* モーダルのループを起こす */
    return TRUE;
}

/* ---- ダイアログのウィンドウプロシージャ ---- */

static BOOL result_via_msgresult(UINT msg) {
    /* これらのメッセージは，ダイアログプロシージャの戻り値をそのまま結果にする */
    switch (msg) {
    case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC: case WM_COMPAREITEM:
    case WM_VKEYTOITEM: case WM_CHARTOITEM: case WM_QUERYDRAGICON: case WM_INITDIALOG:
        return FALSE;
    default:
        return TRUE;
    }
}

LRESULT WINAPI DefDlgProcW(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam) {
    DLGPROC proc = (DLGPROC)GetWindowLongPtrW(hDlg, DWLP_DLGPROC);
    if (proc) {
        SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
        INT_PTR r = proc(hDlg, Msg, wParam, lParam);
        if (r) return result_via_msgresult(Msg) ? GetWindowLongPtrW(hDlg, DWLP_MSGRESULT) : r;
    }

    dialog_info* d = info_of(hDlg);
    switch (Msg) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        FillRect((HDC)wParam, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_SETFONT:
        if (d) d->font = (HFONT)wParam;
        return 0;
    case WM_GETFONT:
        return d ? (LRESULT)d->font : 0;
    case WM_CLOSE: {
        /* 「キャンセル」と同じ扱い */
        HWND cancel = GetDlgItem(hDlg, IDCANCEL);
        if (!cancel || IsWindowEnabled(cancel)) PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)cancel);
        return 0;
    }
    case DM_GETDEFID:
        return d ? MAKELRESULT(d->default_id, DC_HASDEFID) : 0;
    case DM_SETDEFID:
        if (d) d->default_id = (UINT)wParam;
        return TRUE;
    case WM_NCDESTROY:
        free_info(hDlg);
        return 0;
    }
    return DefWindowProcW(hDlg, Msg, wParam, lParam);
}

/* ---- キー操作 ---- */

HWND WINAPI GetNextDlgTabItem(HWND hDlg, HWND hCtl, BOOL bPrevious) {
    /* タブ位置のあるコントロールを作成順に並べ，hCtl の次 (前) を返す */
    HWND items[256];
    int n = 0, current = -1;
    for (HWND c = GetWindow(hDlg, GW_CHILD); c && n < 256; c = GetWindow(c, GW_HWNDNEXT)) {
        LONG style = GetWindowLongW(c, GWL_STYLE);
        if (!(style & WS_TABSTOP) || !(style & WS_VISIBLE) || (style & WS_DISABLED)) continue;
        if (c == hCtl) current = n;
        items[n++] = c;
    }
    if (n == 0) return NULL;
    if (current < 0) return items[bPrevious ? n - 1 : 0];
    return items[(current + (bPrevious ? n - 1 : 1)) % n];
}

BOOL WINAPI IsDialogMessageW(HWND hDlg, LPMSG lpMsg) {
    if (!hDlg || !lpMsg->hwnd || (lpMsg->hwnd != hDlg && !IsChild(hDlg, lpMsg->hwnd))) return FALSE;

    if (lpMsg->message == WM_KEYDOWN) {
        switch (lpMsg->wParam) {
        case VK_RETURN: {
            /* フォーカスのあるボタン，無ければ既定のボタンを押す */
            HWND focus = GetFocus();
            UINT dlgc = focus && IsChild(hDlg, focus) ? (UINT)SendMessageW(focus, WM_GETDLGCODE, 0, 0) : 0;
            UINT id = (dlgc & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) ? (UINT)GetDlgCtrlID(focus)
                                                                          : LOWORD(SendMessageW(hDlg, DM_GETDEFID, 0, 0));
            HWND button = GetDlgItem(hDlg, (int)id);
            if (!button || IsWindowEnabled(button)) {
                SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)button);
            }
            return TRUE;
        }
        case VK_ESCAPE: {
            HWND cancel = GetDlgItem(hDlg, IDCANCEL);
            SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)cancel);
            return TRUE;
        }
        case VK_TAB: {
            HWND next = GetNextDlgTabItem(hDlg, GetFocus(), GetKeyState(VK_SHIFT) < 0);
            if (next) SetFocus(next);
            return TRUE;
        }
        }
    }
    /* Enter / Esc / Tab の文字は上で処理したので捨てる */
    if (lpMsg->message == WM_CHAR && (lpMsg->wParam == '\r' || lpMsg->wParam == 0x1B || lpMsg->wParam == '\t')) {
        return TRUE;
    }

    TranslateMessage(lpMsg);
    DispatchMessageW(lpMsg);
    return TRUE;
}

/* ---- コントロールの操作 ---- */

HWND WINAPI GetDlgItem(HWND hDlg, int nIDDlgItem) {
    for (HWND c = GetWindow(hDlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (GetDlgCtrlID(c) == nIDDlgItem) return c;
    }
    RtlSetLastWin32Error(ERROR_CONTROL_ID_NOT_FOUND);
    return NULL;
}

LRESULT WINAPI SendDlgItemMessageW(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam) {
    HWND item = GetDlgItem(hDlg, nIDDlgItem);
    return item ? SendMessageW(item, Msg, wParam, lParam) : 0;
}

BOOL WINAPI SetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPCWSTR lpString) {
    HWND item = GetDlgItem(hDlg, nIDDlgItem);
    return item ? SetWindowTextW(item, lpString) : FALSE;
}

UINT WINAPI GetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPWSTR lpString, int cchMax) {
    HWND item = GetDlgItem(hDlg, nIDDlgItem);
    if (!item) {
        if (cchMax > 0) lpString[0] = 0;
        return 0;
    }
    return (UINT)GetWindowTextW(item, lpString, cchMax);
}

BOOL WINAPI CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck) {
    SendDlgItemMessageW(hDlg, nIDButton, BM_SETCHECK, uCheck, 0);
    return TRUE;
}

UINT WINAPI IsDlgButtonChecked(HWND hDlg, int nIDButton) {
    return (UINT)SendDlgItemMessageW(hDlg, nIDButton, BM_GETCHECK, 0, 0);
}

/* ---- クラスの登録 ---- */

void register_dialog_classes(HINSTANCE user32) {
    WNDCLASSEXW wc = {
        .cbSize = sizeof(wc),
        .style = CS_GLOBALCLASS | CS_SAVEBITS | CS_DBLCLKS,
        .lpfnWndProc = (WNDPROC)DefDlgProcW,
        .cbWndExtra = DLGWINDOWEXTRA,
        .hInstance = user32,
        .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
        .lpszClassName = dialog_class,
    };
    RegisterClassExW(&wc);
    register_controls(user32);
}
