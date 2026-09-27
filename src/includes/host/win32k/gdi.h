#pragma once
#include <string>
#include "host/win32k/state.h"

// 文字や画像の描画 (CoreText / CoreGraphics)．g_lock を持った状態で呼ぶ

namespace ntmac::win32k::gdi {

struct TextMetrics {
    int32_t height = 0;
    int32_t ascent = 0;
    int32_t descent = 0;
    int32_t internal_leading = 0;
    int32_t external_leading = 0;
    int32_t ave_char_width = 0;
    int32_t max_char_width = 0;
};

TextMetrics metrics(const FontSpec& font);

/** 文字列の幅と高さ (ピクセル) */
void text_extent(const FontSpec& font, std::u16string_view text, int32_t& width, int32_t& height);

/**
 * 描画面に文字列を描く．(x, y) と clip は描画面の座標．揃え方は TA_* (text_align) に従う．
 * bk_opaque なら文字の範囲を bk_color で塗ってから描く．
 */
void draw_text(Surface& surface, const Rect& clip, int32_t x, int32_t y, uint32_t text_align, const FontSpec& font,
               std::u16string_view text, uint32_t color, bool bk_opaque, uint32_t bk_color);

/** 32bpp の画像 (乗算済みでないアルファ) を，描画面にアルファブレンドで描く */
void draw_image(Surface& surface, const Rect& clip, const Rect& dst, const uint32_t* pixels, int32_t width,
                int32_t height);

}
