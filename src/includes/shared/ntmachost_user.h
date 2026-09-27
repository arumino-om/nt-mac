#pragma once
/*
 * NTMACHOST.DLL のウィンドウ・描画のサービス (Windows の win32k に相当)．user32 / gdi32 から呼ばれる．
 * ホスト (clang) と PE 側 (mingw) の両方から使うため C として書き，固定幅の型のみを使う．
 *
 * ハンドル (HWND / HDC / HMENU) はホストが割り当てる値で，PE 側では不透明な値として扱う．
 * 座標は Windows と同じく，メイン画面の左上を原点とし，下向きを正とする．
 */
#include <stdint.h>
#include "shared/ntmachost.h"

enum ntmachost_user_code {
    NTMACHOST_USER_REGISTER_CLASS = 0x100,  /* ntmachost_class_args: Info → Atom */
    NTMACHOST_USER_GET_CLASS_INFO,          /* ntmachost_class_args: Info.ClassName → Info */
    NTMACHOST_USER_CREATE_WINDOW,           /* ntmachost_create_window_args */
    NTMACHOST_USER_DESTROY_WINDOW,          /* ntmachost_window_args */
    NTMACHOST_USER_IS_WINDOW,               /* ntmachost_window_args → Result */
    NTMACHOST_USER_GET_WINDOW_LONG,         /* ntmachost_window_args: Index → Value (無効な Index なら失敗) */
    NTMACHOST_USER_SET_WINDOW_LONG,         /* ntmachost_window_args: Index, Value → Value (以前の値) */
    NTMACHOST_USER_GET_CLASS_LONG,          /* ntmachost_window_args: Index (GCL*_) → Value */
    NTMACHOST_USER_GET_WINDOW_TEXT,         /* ntmachost_text_args */
    NTMACHOST_USER_SET_WINDOW_TEXT,         /* ntmachost_text_args */
    NTMACHOST_USER_SHOW_WINDOW,             /* ntmachost_window_args: Index = SW_* → Result = 以前に表示されていたか */
    NTMACHOST_USER_GET_WINDOW_RECT,         /* ntmachost_rect_args: スクリーン座標 */
    NTMACHOST_USER_GET_CLIENT_RECT,         /* ntmachost_rect_args: クライアント座標 (左上は常に 0,0) */
    NTMACHOST_USER_GET_MESSAGE,             /* ntmachost_msg_args */
    NTMACHOST_USER_POST_MESSAGE,            /* ntmachost_msg_args: Msg */
    NTMACHOST_USER_POST_QUIT_MESSAGE,       /* ntmachost_window_args: Value = 終了コード */
    NTMACHOST_USER_INVALIDATE_RECT,         /* ntmachost_rect_args: Flags = NTMAC_RECT_* */
    NTMACHOST_USER_VALIDATE_RECT,           /* ntmachost_rect_args: Flags = NTMAC_RECT_* */
    NTMACHOST_USER_GET_UPDATE_RECT,         /* ntmachost_rect_args → Rect, Result = 更新領域があるか */
    NTMACHOST_USER_BEGIN_PAINT,             /* ntmachost_paint_args */
    NTMACHOST_USER_END_PAINT,               /* ntmachost_paint_args */
    NTMACHOST_USER_GET_DC,                  /* ntmachost_window_args → Value = HDC */
    NTMACHOST_USER_RELEASE_DC,              /* ntmachost_window_args: Value = HDC */
    NTMACHOST_USER_CREATE_MENU,             /* ntmachost_menu_args: Flags (NTMAC_MENU_POPUP) → Menu */
    NTMACHOST_USER_APPEND_MENU,             /* ntmachost_menu_args: Menu, Flags (MF_*), Item, Text */
    NTMACHOST_USER_DESTROY_MENU,            /* ntmachost_menu_args: Menu */
    NTMACHOST_USER_SET_MENU,                /* ntmachost_menu_args: Window, Menu */
    NTMACHOST_USER_GET_MENU,                /* ntmachost_menu_args: Window → Menu */
    NTMACHOST_USER_GET_KEY_STATE,           /* ntmachost_window_args: Index = 仮想キー → Result (GetKeyState と同じ形) */
    NTMACHOST_USER_GET_SCREEN_INFO,         /* ntmachost_screen_args */
    NTMACHOST_USER_GET_RELATED_WINDOW,      /* ntmachost_window_args: Window, Index = GW_* → Value */
    NTMACHOST_USER_SET_FOCUS,               /* ntmachost_window_args: Window → Value = 以前のフォーカス */
    NTMACHOST_USER_GET_FOCUS,               /* ntmachost_window_args → Value */
    NTMACHOST_USER_SET_CAPTURE,             /* ntmachost_window_args: Window (0 なら解除) → Value = 以前のキャプチャ */
    NTMACHOST_USER_GET_CAPTURE,             /* ntmachost_window_args → Value */
    NTMACHOST_USER_SET_TIMER,               /* ntmachost_timer_args → Id */
    NTMACHOST_USER_KILL_TIMER,              /* ntmachost_timer_args: Window, Id */

    NTMACHOST_GDI_FILL_RECT = 0x200,        /* ntmachost_fill_args */
    NTMACHOST_GDI_SET_DC_STATE,             /* ntmachost_dc_state_args */
    NTMACHOST_GDI_SELECT_FONT,              /* ntmachost_font_args */
    NTMACHOST_GDI_TEXT_OUT,                 /* ntmachost_text_out_args */
    NTMACHOST_GDI_GET_TEXT_EXTENT,          /* ntmachost_text_out_args: Dc, Text, Count → Width, Height */
    NTMACHOST_GDI_GET_TEXT_METRICS,         /* ntmachost_text_metrics_args */
    NTMACHOST_GDI_DRAW_IMAGE,               /* ntmachost_image_args */
};

typedef struct ntmac_rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} ntmac_rect;

/* WNDCLASSEXW 相当．文字列は整数 ID (IS_INTRESOURCE) のこともある */
typedef struct ntmachost_class_info {
    uint32_t Style;
    int32_t ClassExtra;
    int32_t WindowExtra;
    uint32_t _pad;
    void* WndProc;
    uint64_t Instance;
    uint64_t Icon;
    uint64_t Cursor;
    uint64_t Background;
    uint64_t IconSmall;
    const uint16_t* MenuName;   /* GET_CLASS_INFO ではホストが保持する文字列を指す */
    const uint16_t* ClassName;  /* 名前かアトム */
} ntmachost_class_info;

typedef struct ntmachost_class_args {
    ntmachost_class_info Info;
    uint32_t Atom;
    uint32_t _pad;
} ntmachost_class_args;

typedef struct ntmachost_create_window_args {
    uint32_t ExStyle;
    uint32_t Style;
    const uint16_t* ClassName;  /* 名前かアトム */
    const uint16_t* WindowName;
    int32_t X, Y, Width, Height; /* CW_USEDEFAULT (0x80000000) ならホストが決める */
    uint64_t Parent;
    uint64_t Menu;              /* トップレベルならメニュー，子ウィンドウなら ID */
    uint64_t Instance;
    uint64_t Window;            /* out */
    void* WndProc;              /* out: クラスのウィンドウプロシージャ */
} ntmachost_create_window_args;

typedef struct ntmachost_window_args {
    uint64_t Window;
    int32_t Index;
    uint32_t Size;              /* GET/SET_WINDOW_LONG の追加領域 (Index >= 0) の大きさ: 4 か 8 */
    int64_t Value;
    int64_t Result;
} ntmachost_window_args;

typedef struct ntmachost_text_args {
    uint64_t Window;
    uint16_t* Buffer;           /* SET では終端付きの文字列 */
    uint32_t Length;            /* GET: in = Buffer の文字数 (終端を含む)，out = 文字列の長さ */
    uint32_t _pad;
} ntmachost_text_args;

#define NTMAC_RECT_WHOLE  0x1   /* Rect を使わず，クライアント領域全体 */
#define NTMAC_RECT_ERASE  0x2   /* 背景の消去 (WM_ERASEBKGND) も必要にする */

typedef struct ntmachost_rect_args {
    uint64_t Window;
    ntmac_rect Rect;
    uint32_t Flags;
    uint32_t Result;
} ntmachost_rect_args;

typedef struct ntmachost_msg {
    uint64_t Window;
    uint32_t Message;
    uint32_t _pad;
    uint64_t WParam;
    int64_t LParam;
    uint32_t Time;
    int32_t PtX;
    int32_t PtY;
    uint32_t _pad2;
} ntmachost_msg;

#define NTMAC_GET_REMOVE  0x1   /* PM_REMOVE と同じ */
#define NTMAC_GET_WAIT    0x100 /* メッセージが来るまで待つ (GetMessage) */

#define NTMAC_MSG_NONE    0     /* メッセージが無い */
#define NTMAC_MSG_POSTED  1     /* 通常のメッセージ (WM_PAINT などの生成されたものを含む) */
#define NTMAC_MSG_SENT    2     /* ホストから送られたメッセージ．アプリには返さず，ウィンドウプロシージャを呼ぶ */
#define NTMAC_MSG_QUIT    3     /* WM_QUIT (Msg.WParam が終了コード) */

typedef struct ntmachost_msg_args {
    ntmachost_msg Msg;
    uint64_t FilterWindow;
    uint32_t FilterMin;
    uint32_t FilterMax;
    uint32_t Flags;
    uint32_t Result;
} ntmachost_msg_args;

typedef struct ntmachost_paint_args {
    uint64_t Window;
    uint64_t Dc;
    ntmac_rect Paint;
    uint32_t Erase;
    uint32_t _pad;
} ntmachost_paint_args;

#define NTMAC_MENU_POPUP  0x1

typedef struct ntmachost_menu_args {
    uint64_t Menu;
    uint64_t Window;
    uint64_t Item;              /* コマンド ID か，MF_POPUP ならサブメニュー */
    const uint16_t* Text;
    uint32_t Flags;
    uint32_t _pad;
} ntmachost_menu_args;

typedef struct ntmachost_screen_args {
    ntmac_rect Screen;
    ntmac_rect WorkArea;        /* メニューバーと Dock を除いた領域 */
    uint32_t Dpi;
    uint32_t _pad;
} ntmachost_screen_args;

typedef struct ntmachost_fill_args {
    uint64_t Dc;
    ntmac_rect Rect;            /* 論理座標 (クライアント座標) */
    uint32_t Color;             /* COLORREF (0x00BBGGRR) */
    uint32_t _pad;
} ntmachost_fill_args;

typedef struct ntmachost_timer_args {
    uint64_t Window;            /* 0 ならスレッドのタイマー */
    uint64_t Id;                /* Window が 0 のときはホストが割り当てる */
    uint32_t Interval;          /* ミリ秒 */
    uint32_t _pad;
    void* Proc;                 /* TIMERPROC (WM_TIMER の lParam) */
} ntmachost_timer_args;

/* LOGFONTW と同じ配置 */
typedef struct ntmac_logfont {
    int32_t Height;
    int32_t Width;
    int32_t Escapement;
    int32_t Orientation;
    int32_t Weight;
    uint8_t Italic;
    uint8_t Underline;
    uint8_t StrikeOut;
    uint8_t CharSet;
    uint8_t OutPrecision;
    uint8_t ClipPrecision;
    uint8_t Quality;
    uint8_t PitchAndFamily;
    uint16_t FaceName[32];
} ntmac_logfont;

/* 変更する項目 (Mask) */
#define NTMAC_DC_TEXT_COLOR 0x1
#define NTMAC_DC_BK_COLOR   0x2
#define NTMAC_DC_BK_MODE    0x4
#define NTMAC_DC_TEXT_ALIGN 0x8

/* 指定した項目を設定し，全項目の以前の値を返す */
typedef struct ntmachost_dc_state_args {
    uint64_t Dc;
    uint32_t Mask;
    uint32_t TextColor;         /* COLORREF */
    uint32_t BkColor;           /* COLORREF */
    uint32_t BkMode;            /* TRANSPARENT (1) / OPAQUE (2) */
    uint32_t TextAlign;         /* TA_* */
    uint32_t _pad;
} ntmachost_dc_state_args;

/* DC のフォントを選択する．Font は PE 側のフォントのハンドル (0 ならシステムフォント)．以前のハンドルを返す */
typedef struct ntmachost_font_args {
    uint64_t Dc;
    uint64_t Font;
    ntmac_logfont LogFont;
    uint64_t OldFont;           /* out */
} ntmachost_font_args;

#define NTMAC_TEXT_OPAQUE  0x2  /* ETO_OPAQUE と同じ．Rect を背景色で塗る */
#define NTMAC_TEXT_CLIPPED 0x4  /* ETO_CLIPPED と同じ．Rect で切り取る */

typedef struct ntmachost_text_out_args {
    uint64_t Dc;
    int32_t X;
    int32_t Y;
    uint32_t Options;
    uint32_t Count;             /* 文字数 (UTF-16) */
    ntmac_rect Rect;
    const uint16_t* Text;
    int32_t Width;              /* GET_TEXT_EXTENT の結果 */
    int32_t Height;
} ntmachost_text_out_args;

/* TEXTMETRICW の主な項目 */
typedef struct ntmachost_text_metrics_args {
    uint64_t Dc;
    int32_t Height;
    int32_t Ascent;
    int32_t Descent;
    int32_t InternalLeading;
    int32_t ExternalLeading;
    int32_t AveCharWidth;
    int32_t MaxCharWidth;
    int32_t Weight;
    uint8_t Italic;
    uint8_t Underlined;
    uint8_t CharSet;
    uint8_t PitchAndFamily;
    uint32_t _pad;
} ntmachost_text_metrics_args;

/* 32bpp (BGRA，アルファは乗算済みでない) の画像を，アルファブレンドで描く (アイコンなど) */
typedef struct ntmachost_image_args {
    uint64_t Dc;
    int32_t X;
    int32_t Y;
    int32_t Width;              /* 描く大きさ (画像と違えば拡大縮小する) */
    int32_t Height;
    int32_t ImageWidth;
    int32_t ImageHeight;
    const uint32_t* Pixels;     /* 上の行から順 */
} ntmachost_image_args;

NTMAC_STATIC_ASSERT(sizeof(ntmac_logfont) == 92);
NTMAC_STATIC_ASSERT(sizeof(ntmachost_msg) == 48);
NTMAC_STATIC_ASSERT(sizeof(ntmachost_create_window_args) == 80);
