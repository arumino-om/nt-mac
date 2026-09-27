#include "gdi32_private.h"

/* DC の状態とテキストの描画．DC 自体はホストにある */

static uint32_t set_state(HDC hdc, uint32_t mask, uint32_t value, uint32_t* old) {
    ntmachost_dc_state_args args = {.Dc = HANDLE_VALUE(hdc), .Mask = mask};
    switch (mask) {
    case NTMAC_DC_TEXT_COLOR: args.TextColor = value; break;
    case NTMAC_DC_BK_COLOR:   args.BkColor = value; break;
    case NTMAC_DC_BK_MODE:    args.BkMode = value; break;
    case NTMAC_DC_TEXT_ALIGN: args.TextAlign = value; break;
    }
    if (!gdi_call(NTMACHOST_GDI_SET_DC_STATE, &args)) return FALSE;
    switch (mask) {
    case NTMAC_DC_TEXT_COLOR: *old = args.TextColor; break;
    case NTMAC_DC_BK_COLOR:   *old = args.BkColor; break;
    case NTMAC_DC_BK_MODE:    *old = args.BkMode; break;
    default:                  *old = args.TextAlign; break;
    }
    return TRUE;
}

static uint32_t get_state(HDC hdc, uint32_t which) {
    uint32_t old = 0;
    ntmachost_dc_state_args args = {.Dc = HANDLE_VALUE(hdc), .Mask = 0};
    if (!gdi_call(NTMACHOST_GDI_SET_DC_STATE, &args)) return CLR_INVALID;
    switch (which) {
    case NTMAC_DC_TEXT_COLOR: old = args.TextColor; break;
    case NTMAC_DC_BK_COLOR:   old = args.BkColor; break;
    case NTMAC_DC_BK_MODE:    old = args.BkMode; break;
    default:                  old = args.TextAlign; break;
    }
    return old;
}

COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color) {
    uint32_t old;
    return set_state(hdc, NTMAC_DC_TEXT_COLOR, color, &old) ? old : CLR_INVALID;
}

COLORREF WINAPI GetTextColor(HDC hdc) {
    return get_state(hdc, NTMAC_DC_TEXT_COLOR);
}

COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color) {
    uint32_t old;
    return set_state(hdc, NTMAC_DC_BK_COLOR, color, &old) ? old : CLR_INVALID;
}

COLORREF WINAPI GetBkColor(HDC hdc) {
    return get_state(hdc, NTMAC_DC_BK_COLOR);
}

int WINAPI SetBkMode(HDC hdc, int mode) {
    uint32_t old;
    return set_state(hdc, NTMAC_DC_BK_MODE, (uint32_t)mode, &old) ? (int)old : 0;
}

int WINAPI GetBkMode(HDC hdc) {
    return (int)get_state(hdc, NTMAC_DC_BK_MODE);
}

UINT WINAPI SetTextAlign(HDC hdc, UINT align) {
    uint32_t old;
    return set_state(hdc, NTMAC_DC_TEXT_ALIGN, align, &old) ? old : GDI_ERROR;
}

UINT WINAPI GetTextAlign(HDC hdc) {
    return get_state(hdc, NTMAC_DC_TEXT_ALIGN);
}

/* フォントだけを DC に選択できる．ブラシやペンは描画の関数を実装するときに対応する */
HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h) {
    DWORD type = GetObjectType(h);
    if (type != OBJ_FONT) {
        if (type == OBJ_BRUSH) return GetStockObject(WHITE_BRUSH);
        return NULL;
    }

    ntmachost_font_args args = {.Dc = HANDLE_VALUE(hdc), .Font = HANDLE_VALUE(h)};
    LOGFONTW lf;
    font_logfont(h, &lf);
    memcpy(&args.LogFont, &lf, sizeof(args.LogFont));
    if (!gdi_call(NTMACHOST_GDI_SELECT_FONT, &args)) return NULL;
    return args.OldFont ? (HGDIOBJ)(ULONG_PTR)args.OldFont : stock_system_font();
}

HGDIOBJ WINAPI GetCurrentObject(HDC hdc, UINT type) {
    if (type != OBJ_FONT) return NULL;
    /* 現在のフォントを知るため，同じフォントを選択し直す */
    HGDIOBJ current = SelectObject(hdc, stock_system_font());
    SelectObject(hdc, current);
    return current;
}

BOOL WINAPI ExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT* lprect, LPCWSTR lpString, UINT c,
                        const INT* lpDx) {
    (void)lpDx; /* 文字ごとの間隔の指定は未対応 */
    ntmachost_text_out_args args = {
        .Dc = HANDLE_VALUE(hdc),
        .X = x,
        .Y = y,
        .Options = lprect ? (options & (ETO_OPAQUE | ETO_CLIPPED)) : 0,
        .Count = lpString ? c : 0,
        .Text = (const uint16_t*)lpString,
    };
    if (lprect) args.Rect = (ntmac_rect){lprect->left, lprect->top, lprect->right, lprect->bottom};
    return gdi_call(NTMACHOST_GDI_TEXT_OUT, &args);
}

BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c) {
    return ExtTextOutW(hdc, x, y, 0, NULL, lpString, (UINT)c, NULL);
}

BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl) {
    ntmachost_text_out_args args = {.Dc = HANDLE_VALUE(hdc), .Count = (uint32_t)c, .Text = (const uint16_t*)lpString};
    if (!gdi_call(NTMACHOST_GDI_GET_TEXT_EXTENT, &args)) return FALSE;
    psizl->cx = args.Width;
    psizl->cy = args.Height;
    return TRUE;
}

BOOL WINAPI GetTextExtentPointW(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl) {
    return GetTextExtentPoint32W(hdc, lpString, c, psizl);
}

BOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW lptm) {
    ntmachost_text_metrics_args args = {.Dc = HANDLE_VALUE(hdc)};
    if (!gdi_call(NTMACHOST_GDI_GET_TEXT_METRICS, &args)) return FALSE;
    memset(lptm, 0, sizeof(*lptm));
    lptm->tmHeight = args.Height;
    lptm->tmAscent = args.Ascent;
    lptm->tmDescent = args.Descent;
    lptm->tmInternalLeading = args.InternalLeading;
    lptm->tmExternalLeading = args.ExternalLeading;
    lptm->tmAveCharWidth = args.AveCharWidth;
    lptm->tmMaxCharWidth = args.MaxCharWidth;
    lptm->tmWeight = args.Weight;
    lptm->tmItalic = args.Italic;
    lptm->tmUnderlined = args.Underlined;
    lptm->tmCharSet = args.CharSet;
    lptm->tmPitchAndFamily = args.PitchAndFamily;
    lptm->tmFirstChar = 0x20;
    lptm->tmLastChar = 0xFFFC;
    lptm->tmDefaultChar = 0x30FB;
    lptm->tmBreakChar = 0x20;
    lptm->tmDigitizedAspectX = 96;
    lptm->tmDigitizedAspectY = 96;
    return TRUE;
}

int WINAPI GetDeviceCaps(HDC hdc, int index) {
    (void)hdc;
    switch (index) {
    case LOGPIXELSX:
    case LOGPIXELSY:  return 96;
    case BITSPIXEL:   return 32;
    case PLANES:      return 1;
    case NUMCOLORS:   return -1;
    case HORZRES:     return 1440; /* TODO: 画面の大きさ */
    case VERTRES:     return 900;
    case TECHNOLOGY:  return DT_RASDISPLAY;
    case RASTERCAPS:  return RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP;
    default:          return 0;
    }
}
