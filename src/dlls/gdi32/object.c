#include "gdi32_private.h"

/*
 * GDI オブジェクト．今はブラシだけを PE 側 (プロセスヒープ) で持つ．
 * ペン・フォント・ビットマップなどは描画を実装するときに追加する．
 */

#define GDI_MAGIC 0x4E54474Fu /* "NTGO" */

typedef struct gdi_object {
    DWORD magic;
    DWORD type;         /* OBJ_BRUSH / OBJ_FONT */
    BOOL stock;
    COLORREF color;     /* ブラシ */
    BOOL null_brush;
    LOGFONTW font;      /* フォント */
} gdi_object;

#define STOCK_BRUSH(r, g, b, null) {GDI_MAGIC, OBJ_BRUSH, TRUE, RGB(r, g, b), null, {0}}
#define STOCK_FONT(h, w, face) {GDI_MAGIC, OBJ_FONT, TRUE, 0, FALSE, {.lfHeight = h, .lfWeight = w, .lfCharSet = DEFAULT_CHARSET, .lfFaceName = face}}

static gdi_object stock_objects[] = {
    [WHITE_BRUSH] = STOCK_BRUSH(255, 255, 255, FALSE),
    [LTGRAY_BRUSH] = STOCK_BRUSH(192, 192, 192, FALSE),
    [GRAY_BRUSH] = STOCK_BRUSH(128, 128, 128, FALSE),
    [DKGRAY_BRUSH] = STOCK_BRUSH(64, 64, 64, FALSE),
    [BLACK_BRUSH] = STOCK_BRUSH(0, 0, 0, FALSE),
    [NULL_BRUSH] = STOCK_BRUSH(0, 0, 0, TRUE),
    [OEM_FIXED_FONT] = STOCK_FONT(12, FW_NORMAL, L"Terminal"),
    [ANSI_FIXED_FONT] = STOCK_FONT(12, FW_NORMAL, L"Courier"),
    [ANSI_VAR_FONT] = STOCK_FONT(12, FW_NORMAL, L"MS Sans Serif"),
    [SYSTEM_FONT] = STOCK_FONT(16, FW_BOLD, L"System"),
    [DEVICE_DEFAULT_FONT] = STOCK_FONT(16, FW_BOLD, L"System"),
    [SYSTEM_FIXED_FONT] = STOCK_FONT(16, FW_NORMAL, L"Fixedsys"),
    [DEFAULT_GUI_FONT] = STOCK_FONT(-11, FW_NORMAL, L"MS Shell Dlg"),
};

#define NUM_STOCK_OBJECTS (sizeof(stock_objects) / sizeof(stock_objects[0]))

static gdi_object* object_of(HGDIOBJ h) {
    gdi_object* o = (gdi_object*)h;
    if ((ULONG_PTR)h < 0x10000 || o->magic != GDI_MAGIC) return NULL;
    return o;
}

HBRUSH WINAPI CreateSolidBrush(COLORREF color) {
    gdi_object* o = RtlAllocateHeap(RtlGetCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*o));
    if (!o) return NULL;
    o->magic = GDI_MAGIC;
    o->type = OBJ_BRUSH;
    o->color = color & 0x00FFFFFF;
    return (HBRUSH)o;
}

HGDIOBJ WINAPI GetStockObject(int i) {
    if (i >= 0 && (size_t)i < NUM_STOCK_OBJECTS && stock_objects[i].magic) return (HGDIOBJ)&stock_objects[i];
    return NULL;
}

HGDIOBJ stock_system_font(void) {
    return (HGDIOBJ)&stock_objects[SYSTEM_FONT];
}

HFONT WINAPI CreateFontIndirectW(const LOGFONTW* lplf) {
    if (!lplf) return NULL;
    gdi_object* o = RtlAllocateHeap(RtlGetCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*o));
    if (!o) return NULL;
    o->magic = GDI_MAGIC;
    o->type = OBJ_FONT;
    o->font = *lplf;
    return (HFONT)o;
}

HFONT WINAPI CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic,
                         DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
                         DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName) {
    LOGFONTW lf = {
        .lfHeight = cHeight, .lfWidth = cWidth, .lfEscapement = cEscapement, .lfOrientation = cOrientation,
        .lfWeight = cWeight, .lfItalic = (BYTE)bItalic, .lfUnderline = (BYTE)bUnderline,
        .lfStrikeOut = (BYTE)bStrikeOut, .lfCharSet = (BYTE)iCharSet, .lfOutPrecision = (BYTE)iOutPrecision,
        .lfClipPrecision = (BYTE)iClipPrecision, .lfQuality = (BYTE)iQuality, .lfPitchAndFamily = (BYTE)iPitchAndFamily,
    };
    if (pszFaceName) {
        for (int i = 0; i < LF_FACESIZE - 1 && pszFaceName[i]; i++) lf.lfFaceName[i] = pszFaceName[i];
    }
    return CreateFontIndirectW(&lf);
}

BOOL font_logfont(HGDIOBJ font, LOGFONTW* lf) {
    gdi_object* o = object_of(font);
    if (!o || o->type != OBJ_FONT) return FALSE;
    *lf = o->font;
    return TRUE;
}

int WINAPI GetObjectW(HANDLE h, int c, LPVOID pv) {
    gdi_object* o = object_of(h);
    if (!o) return 0;
    if (o->type == OBJ_FONT) {
        if (!pv) return sizeof(LOGFONTW);
        int n = min(c, (int)sizeof(LOGFONTW));
        memcpy(pv, &o->font, (size_t)n);
        return n;
    }
    if (o->type == OBJ_BRUSH) {
        LOGBRUSH lb = {.lbStyle = o->null_brush ? BS_NULL : BS_SOLID, .lbColor = o->color};
        if (!pv) return sizeof(lb);
        int n = min(c, (int)sizeof(lb));
        memcpy(pv, &lb, (size_t)n);
        return n;
    }
    return 0;
}

BOOL WINAPI DeleteObject(HGDIOBJ ho) {
    gdi_object* o = object_of(ho);
    if (!o) return FALSE;
    if (o->stock) return TRUE;
    o->magic = 0;
    return RtlFreeHeap(RtlGetCurrentPeb()->ProcessHeap, 0, o) ? TRUE : FALSE;
}

DWORD WINAPI GetObjectType(HGDIOBJ h) {
    gdi_object* o = object_of(h);
    return o ? o->type : 0;
}

BOOL WINAPI ntmac_GetBrushColor(HBRUSH brush, COLORREF* color) {
    gdi_object* o = object_of(brush);
    if (!o || o->type != OBJ_BRUSH || o->null_brush) return FALSE;
    *color = o->color;
    return TRUE;
}
