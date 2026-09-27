#include "user32_private.h"

/* ---- DrawText ---- */

static BOOL breakable_after(WCHAR c, WCHAR next) {
    /* 空白の後と，CJK の文字の前後で改行できる */
    return c == ' ' || c == '\t' || c >= 0x3000 || next >= 0x3000;
}

static int text_width(HDC hdc, const WCHAR* s, int n) {
    SIZE size = {0};
    if (n > 0) GetTextExtentPoint32W(hdc, s, n, &size);
    return size.cx;
}

/* s[0..n) のうち，幅 max_width に収まる行の長さ (折り返し位置) を返す．少なくとも1文字は含める */
static int fit_line(HDC hdc, const WCHAR* s, int n, int max_width) {
    if (text_width(hdc, s, n) <= max_width) return n;
    int lo = 1, hi = n; /* 収まる最大の文字数を二分探索する */
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (text_width(hdc, s, mid) <= max_width) lo = mid;
        else hi = mid - 1;
    }
    for (int i = lo; i > 0; i--) {
        if (breakable_after(s[i - 1], s[i])) return i;
    }
    return lo; /* 区切れる場所が無ければ文字の途中で折り返す */
}

int WINAPI DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format) {
    if (!lpchText || !lprc) return 0;
    int len = cchText < 0 ? (int)wcslen(lpchText) : cchText;

    /* & (アクセスキー) を取り除く．&& は & にする */
    WCHAR stack_buf[512];
    WCHAR* text = len < 512 ? stack_buf : HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(len + 1) * sizeof(WCHAR));
    if (!text) return 0;
    int n = 0;
    for (int i = 0; i < len; i++) {
        if (!(format & DT_NOPREFIX) && lpchText[i] == '&') {
            if (i + 1 < len && lpchText[i + 1] == '&') text[n++] = '&', i++;
            continue;
        }
        text[n++] = lpchText[i];
    }

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    int line_height = tm.tmHeight;
    int width = lprc->right - lprc->left;
    BOOL single = format & DT_SINGLELINE;
    BOOL calc = format & DT_CALCRECT;
    UINT old_align = SetTextAlign(hdc, TA_LEFT | TA_TOP);

    /* 行に分ける (単一行でなければ改行文字と折り返し) */
    int starts[256], lengths[256], lines = 0, max_width = 0;
    for (int pos = 0; pos <= n && lines < 256;) {
        int end = pos;
        if (!single) {
            while (end < n && text[end] != '\n' && text[end] != '\r') end++;
        } else {
            end = n;
        }
        int line_len = end - pos;
        if ((format & DT_WORDBREAK) && !single && line_len > 0) {
            int fit = fit_line(hdc, text + pos, line_len, width);
            if (fit < line_len) {
                end = pos + fit;
                line_len = fit;
                while (line_len > 0 && text[pos + line_len - 1] == ' ') line_len--; /* 行末の空白は描かない */
            }
        }
        starts[lines] = pos;
        lengths[lines] = line_len;
        max_width = max(max_width, text_width(hdc, text + pos, line_len));
        lines++;

        if (end >= n) break;
        pos = end;
        if (text[pos] == '\r') pos++;
        if (pos < n && text[pos] == '\n') pos++;
        while (!single && (format & DT_WORDBREAK) && pos < n && text[pos] == ' ' && text[pos - 1] != '\n') pos++;
        if (pos >= n && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
            starts[lines] = n;
            lengths[lines] = 0;
            lines++;
            break;
        }
    }
    int total_height = lines * line_height;

    if (calc) {
        if (!(format & DT_WORDBREAK) || single) lprc->right = lprc->left + max_width;
        lprc->bottom = lprc->top + total_height;
    } else {
        int y = lprc->top;
        if (single && (format & DT_VCENTER)) y = lprc->top + (lprc->bottom - lprc->top - line_height) / 2;
        else if (single && (format & DT_BOTTOM)) y = lprc->bottom - line_height;
        for (int i = 0; i < lines; i++, y += line_height) {
            int w = text_width(hdc, text + starts[i], lengths[i]);
            int x = lprc->left;
            if (format & DT_CENTER) x = lprc->left + (width - w) / 2;
            else if (format & DT_RIGHT) x = lprc->right - w;
            ExtTextOutW(hdc, x, y, (format & DT_NOCLIP) ? 0 : ETO_CLIPPED, lprc, text + starts[i], (UINT)lengths[i],
                        NULL);
        }
    }

    SetTextAlign(hdc, old_align);
    if (text != stack_buf) HeapFree(GetProcessHeap(), 0, text);
    return total_height;
}

/* ---- 枠 ---- */

static void fill_color(HDC hdc, int l, int t, int r, int b, int sys_color) {
    RECT rc = {l, t, r, b};
    FillRect(hdc, &rc, GetSysColorBrush(sys_color));
}

int WINAPI FrameRect(HDC hDC, const RECT* lprc, HBRUSH hbr) {
    RECT r;
    SetRect(&r, lprc->left, lprc->top, lprc->right, lprc->top + 1);
    FillRect(hDC, &r, hbr);
    SetRect(&r, lprc->left, lprc->bottom - 1, lprc->right, lprc->bottom);
    FillRect(hDC, &r, hbr);
    SetRect(&r, lprc->left, lprc->top, lprc->left + 1, lprc->bottom);
    FillRect(hDC, &r, hbr);
    SetRect(&r, lprc->right - 1, lprc->top, lprc->right, lprc->bottom);
    FillRect(hDC, &r, hbr);
    return 1;
}

/* 立体的な枠．外側と内側の2本の線を，左上と右下で色を変えて描く */
BOOL WINAPI DrawEdge(HDC hdc, LPRECT qrc, UINT edge, UINT grfFlags) {
    RECT r = *qrc;
    struct { UINT raised, sunken; int light, dark; } rings[2] = {
        {BDR_RAISEDOUTER, BDR_SUNKENOUTER, COLOR_3DLIGHT, COLOR_3DDKSHADOW},
        {BDR_RAISEDINNER, BDR_SUNKENINNER, COLOR_BTNHIGHLIGHT, COLOR_BTNSHADOW},
    };
    for (int i = 0; i < 2; i++) {
        int tl, br;
        if (edge & rings[i].raised) tl = rings[i].light, br = rings[i].dark;
        else if (edge & rings[i].sunken) tl = rings[i].dark, br = rings[i].light;
        else continue;
        if (grfFlags & BF_TOP) fill_color(hdc, r.left, r.top, r.right, r.top + 1, tl);
        if (grfFlags & BF_LEFT) fill_color(hdc, r.left, r.top, r.left + 1, r.bottom, tl);
        if (grfFlags & BF_BOTTOM) fill_color(hdc, r.left, r.bottom - 1, r.right, r.bottom, br);
        if (grfFlags & BF_RIGHT) fill_color(hdc, r.right - 1, r.top, r.right, r.bottom, br);
        InflateRect(&r, -1, -1);
    }
    if (grfFlags & BF_MIDDLE) FillRect(hdc, &r, GetSysColorBrush(COLOR_BTNFACE));
    if (grfFlags & BF_ADJUST) *qrc = r;
    return TRUE;
}

/* フォーカスの点線枠．XOR の描画が無いので今は描かない */
BOOL WINAPI DrawFocusRect(HDC hDC, const RECT* lprc) {
    (void)hDC;
    (void)lprc;
    return TRUE;
}
