#include "user32_private.h"

/*
 * アイコン．リソースのアイコン (DIB 形式) を 32bpp (BGRA，乗算済みでないアルファ) に展開して持つ．
 * HICON はプロセスヒープ上の icon_object を指す．システムのアイコン (IDI_*) は空のアイコンにする．
 */

#define ICON_MAGIC 0x4E544943u /* "NTIC" */

typedef struct icon_object {
    DWORD magic;
    int width;
    int height;
    DWORD pixels[];
} icon_object;

#pragma pack(push, 2)
typedef struct group_icon_entry {
    BYTE width;
    BYTE height;
    BYTE colors;
    BYTE reserved;
    WORD planes;
    WORD bit_count;
    DWORD bytes;
    WORD id;
} group_icon_entry;

typedef struct group_icon_dir {
    WORD reserved;
    WORD type;
    WORD count;
    group_icon_entry entries[];
} group_icon_dir;
#pragma pack(pop)

static icon_object* icon_of(HICON h) {
    icon_object* o = (icon_object*)h;
    if ((ULONG_PTR)h < 0x10000 || o->magic != ICON_MAGIC) return NULL;
    return o;
}

static icon_object* alloc_icon(int w, int h) {
    icon_object* o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(icon_object) + (SIZE_T)w * h * sizeof(DWORD));
    if (!o) return NULL;
    o->magic = ICON_MAGIC;
    o->width = w;
    o->height = h;
    return o;
}

/* RT_ICON のデータ (BITMAPINFOHEADER + パレット + 色のビット + AND マスク) を展開する */
static icon_object* decode_dib(const BYTE* data, DWORD size) {
    if (size < sizeof(BITMAPINFOHEADER) || !memcmp(data, "\x89PNG", 4)) return NULL; /* PNG は未対応 */
    const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)data;
    int w = bih->biWidth, h = bih->biHeight / 2, bpp = bih->biBitCount;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) {
        return NULL;
    }

    const RGBQUAD* palette = (const RGBQUAD*)(data + bih->biSize);
    int colors = bpp <= 8 ? (bih->biClrUsed ? (int)bih->biClrUsed : 1 << bpp) : 0;
    const BYTE* xor_bits = (const BYTE*)(palette + colors);
    int xor_stride = ((w * bpp + 31) / 32) * 4;
    const BYTE* and_bits = xor_bits + (size_t)xor_stride * h;
    int and_stride = ((w + 31) / 32) * 4;
    BOOL has_and = and_bits + (size_t)and_stride * h <= data + size;

    icon_object* icon = alloc_icon(w, h);
    if (!icon) return NULL;

    BOOL any_alpha = FALSE;
    for (int y = 0; y < h; y++) {
        const BYTE* row = xor_bits + (size_t)xor_stride * (h - 1 - y); /* DIB は下の行から */
        for (int x = 0; x < w; x++) {
            DWORD c;
            if (bpp == 32) {
                c = ((const DWORD*)row)[x];
                if (c >> 24) any_alpha = TRUE;
            } else if (bpp == 24) {
                c = row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16);
            } else {
                int index = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> ((x & 1) ? 0 : 4)) & 0xF
                                                         : (row[x / 8] >> (7 - (x & 7))) & 1;
                RGBQUAD q = index < colors ? palette[index] : (RGBQUAD){0};
                c = q.rgbBlue | (q.rgbGreen << 8) | (q.rgbRed << 16);
            }
            icon->pixels[y * w + x] = c;
        }
    }

    /* アルファを持たない形式は，AND マスク (1 = 透明) から不透明度を作る */
    if (!any_alpha) {
        for (int y = 0; y < h; y++) {
            const BYTE* row = and_bits + (size_t)and_stride * (h - 1 - y);
            for (int x = 0; x < w; x++) {
                BOOL transparent = has_and && ((row[x / 8] >> (7 - (x & 7))) & 1);
                icon->pixels[y * w + x] = (icon->pixels[y * w + x] & 0xFFFFFF) | (transparent ? 0 : 0xFF000000u);
            }
        }
    }
    return icon;
}

/* グループアイコンから，大きさが size に最も近く，色数の多いものを選んで読み込む */
static HICON load_icon(HINSTANCE inst, LPCWSTR name, int size) {
    HMODULE module = inst ? inst : GetModuleHandleW(NULL);
    HRSRC res = FindResourceW(module, name, (LPCWSTR)RT_GROUP_ICON);
    if (!res) return NULL;
    const group_icon_dir* dir = LockResource(LoadResource(module, res));

    int best = -1, best_score = 0x7FFFFFFF;
    for (int i = 0; i < dir->count; i++) {
        const group_icon_entry* e = &dir->entries[i];
        int w = e->width ? e->width : 256;
        int score = (w > size ? w - size : size - w) * 64 - e->bit_count;
        if (score < best_score) {
            HRSRC r = FindResourceW(module, MAKEINTRESOURCEW(e->id), (LPCWSTR)RT_ICON);
            const BYTE* data = r ? LockResource(LoadResource(module, r)) : NULL;
            if (data && memcmp(data, "\x89PNG", 4) != 0) {
                best = i;
                best_score = score;
            }
        }
    }
    if (best < 0) return NULL;

    HRSRC r = FindResourceW(module, MAKEINTRESOURCEW(dir->entries[best].id), (LPCWSTR)RT_ICON);
    return (HICON)decode_dib(LockResource(LoadResource(module, r)), SizeofResource(module, r));
}

HICON WINAPI LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName) {
    if (!hInstance) return (HICON)alloc_icon(1, 1); /* システムのアイコン (未実装．透明な 1x1) */
    HICON icon = load_icon(hInstance, lpIconName, GetSystemMetrics(SM_CXICON));
    if (!icon) RtlSetLastWin32Error(ERROR_RESOURCE_NAME_NOT_FOUND);
    return icon;
}

HANDLE WINAPI LoadImageW(HINSTANCE hInst, LPCWSTR name, UINT type, int cx, int cy, UINT fuLoad) {
    (void)cy;
    (void)fuLoad;
    if (type == IMAGE_ICON) {
        if (!hInst) return (HANDLE)alloc_icon(1, 1);
        return (HANDLE)load_icon(hInst, name, cx ? cx : GetSystemMetrics(SM_CXICON));
    }
    if (type == IMAGE_CURSOR) return (HANDLE)LoadCursorW(hInst, name);
    RtlSetLastWin32Error(ERROR_CALL_NOT_IMPLEMENTED); /* ビットマップは未対応 */
    return NULL;
}

BOOL WINAPI DestroyIcon(HICON hIcon) {
    icon_object* o = icon_of(hIcon);
    if (!o) return FALSE;
    o->magic = 0;
    return HeapFree(GetProcessHeap(), 0, o);
}

BOOL WINAPI DrawIconEx(HDC hdc, int xLeft, int yTop, HICON hIcon, int cxWidth, int cyWidth, UINT istepIfAniCur,
                       HBRUSH hbrFlickerFreeDraw, UINT diFlags) {
    (void)istepIfAniCur;
    (void)hbrFlickerFreeDraw;
    (void)diFlags;
    icon_object* o = icon_of(hIcon);
    if (!o) return FALSE;
    ntmachost_image_args args = {
        .Dc = HANDLE_VALUE(hdc),
        .X = xLeft,
        .Y = yTop,
        .Width = cxWidth ? cxWidth : o->width,
        .Height = cyWidth ? cyWidth : o->height,
        .ImageWidth = o->width,
        .ImageHeight = o->height,
        .Pixels = (const uint32_t*)o->pixels,
    };
    return user_call(NTMACHOST_GDI_DRAW_IMAGE, &args);
}

BOOL WINAPI DrawIcon(HDC hDC, int X, int Y, HICON hIcon) {
    return DrawIconEx(hDC, X, Y, hIcon, 0, 0, 0, NULL, DI_NORMAL);
}
