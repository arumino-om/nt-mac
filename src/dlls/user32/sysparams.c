#include "user32_private.h"

/* システムカラー (Windows 10 の既定値) */
static const COLORREF system_colors[] = {
    [COLOR_SCROLLBAR] = RGB(200, 200, 200),
    [COLOR_BACKGROUND] = RGB(0, 0, 0),
    [COLOR_ACTIVECAPTION] = RGB(153, 180, 209),
    [COLOR_INACTIVECAPTION] = RGB(191, 205, 219),
    [COLOR_MENU] = RGB(240, 240, 240),
    [COLOR_WINDOW] = RGB(255, 255, 255),
    [COLOR_WINDOWFRAME] = RGB(100, 100, 100),
    [COLOR_MENUTEXT] = RGB(0, 0, 0),
    [COLOR_WINDOWTEXT] = RGB(0, 0, 0),
    [COLOR_CAPTIONTEXT] = RGB(0, 0, 0),
    [COLOR_ACTIVEBORDER] = RGB(180, 180, 180),
    [COLOR_INACTIVEBORDER] = RGB(244, 247, 252),
    [COLOR_APPWORKSPACE] = RGB(171, 171, 171),
    [COLOR_HIGHLIGHT] = RGB(0, 120, 215),
    [COLOR_HIGHLIGHTTEXT] = RGB(255, 255, 255),
    [COLOR_BTNFACE] = RGB(240, 240, 240),
    [COLOR_BTNSHADOW] = RGB(160, 160, 160),
    [COLOR_GRAYTEXT] = RGB(109, 109, 109),
    [COLOR_BTNTEXT] = RGB(0, 0, 0),
    [COLOR_INACTIVECAPTIONTEXT] = RGB(0, 0, 0),
    [COLOR_BTNHIGHLIGHT] = RGB(255, 255, 255),
    [COLOR_3DDKSHADOW] = RGB(105, 105, 105),
    [COLOR_3DLIGHT] = RGB(227, 227, 227),
    [COLOR_INFOTEXT] = RGB(0, 0, 0),
    [COLOR_INFOBK] = RGB(255, 255, 225),
    [COLOR_HOTLIGHT] = RGB(0, 102, 204),
    [COLOR_GRADIENTACTIVECAPTION] = RGB(185, 209, 234),
    [COLOR_GRADIENTINACTIVECAPTION] = RGB(215, 228, 242),
    [COLOR_MENUHILIGHT] = RGB(0, 120, 215),
    [COLOR_MENUBAR] = RGB(240, 240, 240),
};

#define NUM_SYSTEM_COLORS (sizeof(system_colors) / sizeof(system_colors[0]))

DWORD WINAPI GetSysColor(int nIndex) {
    return nIndex >= 0 && (size_t)nIndex < NUM_SYSTEM_COLORS ? system_colors[nIndex] : 0;
}

/* システムカラーのブラシは (HBRUSH)(色番号 + 1) で表す (クラスの hbrBackground と同じ形)．FillRect が解釈する */
HBRUSH WINAPI GetSysColorBrush(int nIndex) {
    return nIndex >= 0 && (size_t)nIndex < NUM_SYSTEM_COLORS ? (HBRUSH)(ULONG_PTR)(nIndex + 1) : NULL;
}

BOOL brush_color(HBRUSH brush, COLORREF* color) {
    ULONG_PTR v = (ULONG_PTR)brush;
    if (v >= 1 && v <= NUM_SYSTEM_COLORS) {
        *color = system_colors[v - 1];
        return TRUE;
    }
    return ntmac_GetBrushColor(brush, color);
}

static ntmachost_screen_args screen_info(void) {
    ntmachost_screen_args args = {0};
    ntmachost_call(NTMACHOST_USER_GET_SCREEN_INFO, &args);
    return args;
}

int WINAPI GetSystemMetrics(int nIndex) {
    ntmachost_screen_args s;
    switch (nIndex) {
    case SM_CXSCREEN:
    case SM_CXVIRTUALSCREEN:
    case SM_CXFULLSCREEN:
        s = screen_info();
        return s.Screen.right - s.Screen.left;
    case SM_CYSCREEN:
    case SM_CYVIRTUALSCREEN:
        s = screen_info();
        return s.Screen.bottom - s.Screen.top;
    case SM_CYFULLSCREEN:
        s = screen_info();
        return s.WorkArea.bottom - s.WorkArea.top - NTMAC_CAPTION_HEIGHT;
    case SM_CXMAXIMIZED:
        s = screen_info();
        return s.WorkArea.right - s.WorkArea.left;
    case SM_CYMAXIMIZED:
        s = screen_info();
        return s.WorkArea.bottom - s.WorkArea.top;
    case SM_CYCAPTION:      return NTMAC_CAPTION_HEIGHT;
    case SM_CXBORDER:
    case SM_CYBORDER:       return 1;
    case SM_CXEDGE:
    case SM_CYEDGE:         return 2;
    case SM_CXFRAME:
    case SM_CYFRAME:
    case SM_CXDLGFRAME:
    case SM_CYDLGFRAME:     return 0; /* 枠は NSWindow が描くので，ウィンドウの大きさには含めない */
    case SM_CYMENU:         return 0; /* メニューは画面上部のメニューバーに出す */
    case SM_CXICON:
    case SM_CYICON:         return 32;
    case SM_CXSMICON:
    case SM_CYSMICON:       return 16;
    case SM_CXCURSOR:
    case SM_CYCURSOR:       return 32;
    case SM_CXVSCROLL:
    case SM_CYHSCROLL:      return 17;
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK:    return 4;
    case SM_CMOUSEBUTTONS:  return 3;
    case SM_MOUSEPRESENT:
    case SM_MOUSEWHEELPRESENT: return 1;
    case SM_CMONITORS:      return 1;
    default:                return 0;
    }
}

BOOL WINAPI SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) {
    (void)uiParam;
    (void)fWinIni;
    switch (uiAction) {
    case SPI_GETWORKAREA: {
        ntmachost_screen_args s = screen_info();
        RECT* r = pvParam;
        r->left = s.WorkArea.left;
        r->top = s.WorkArea.top;
        r->right = s.WorkArea.right;
        r->bottom = s.WorkArea.bottom;
        return TRUE;
    }
    default:
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
}

SHORT WINAPI GetKeyState(int nVirtKey) {
    ntmachost_window_args args = {.Index = nVirtKey};
    ntmachost_call(NTMACHOST_USER_GET_KEY_STATE, &args);
    return (SHORT)args.Result;
}

SHORT WINAPI GetAsyncKeyState(int vKey) {
    return GetKeyState(vKey) & (SHORT)0x8000;
}
