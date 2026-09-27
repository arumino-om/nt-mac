#include <CoreText/CoreText.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include "host/win32k/gdi.h"

namespace ntmac::win32k::gdi {

namespace {

char16_t lower(char16_t c) {
    return c >= u'A' && c <= u'Z' ? c + (u'a' - u'A') : c;
}

std::u16string lowercase(std::u16string_view s) {
    std::u16string out(s);
    for (auto& c : out) c = lower(c);
    return out;
}

/** Windows のフォント名を macOS のフォントのファミリ名にする */
CFStringRef mac_family(std::u16string_view face) {
    auto f = lowercase(face);
    if (f.find(u"mincho") != std::u16string::npos || f.find(u"明朝") != std::u16string::npos) {
        return CFSTR("Hiragino Mincho ProN");
    }
    if (f == u"arial" || f == u"helvetica") return CFSTR("Helvetica");
    if (f == u"times new roman") return CFSTR("Times New Roman");
    if (f == u"courier new" || f == u"courier") return CFSTR("Courier New");
    if (f == u"consolas" || f == u"lucida console") return CFSTR("Menlo");
    // UI 用のフォント (MS Shell Dlg，MS UI Gothic，Meiryo，Segoe UI，System など) と，見つからないフォント
    return CFSTR("Hiragino Sans");
}

struct FontKey {
    std::u16string family;
    int32_t height;
    bool bold;
    bool italic;
    bool operator<(const FontKey& o) const {
        return std::tie(family, height, bold, italic) < std::tie(o.family, o.height, o.bold, o.italic);
    }
};

/** LOGFONT の高さから CoreText の文字の大きさを求める (正の高さはセルの高さ = ascent + descent) */
CGFloat em_size(CFStringRef family, int32_t height) {
    if (height < 0) return -height;
    if (height == 0) return 12;
    CTFontRef probe = CTFontCreateWithName(family, 100, nullptr);
    CGFloat cell = CTFontGetAscent(probe) + CTFontGetDescent(probe);
    CFRelease(probe);
    return cell > 0 ? height * 100 / cell : height;
}

CTFontRef font_for(const FontSpec& spec) {
    static std::map<FontKey, CTFontRef> cache; // g_lock で保護される
    CFStringRef family = mac_family(spec.face);
    FontKey key{lowercase(spec.face), spec.height, spec.weight >= 600, spec.italic};
    if (auto it = cache.find(key); it != cache.end()) return it->second;

    CTFontRef font = CTFontCreateWithName(family, em_size(family, spec.height), nullptr);
    CTFontSymbolicTraits traits = (key.bold ? kCTFontBoldTrait : 0) | (key.italic ? kCTFontItalicTrait : 0);
    if (traits) {
        if (CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(font, 0, nullptr, traits, traits)) {
            CFRelease(font);
            font = styled;
        }
    }
    cache[key] = font;
    return font;
}

CTLineRef make_line(CTFontRef font, std::u16string_view text, CGColorRef color) {
    CFStringRef str = CFStringCreateWithCharacters(nullptr, reinterpret_cast<const UniChar*>(text.data()),
                                                   static_cast<CFIndex>(text.size()));
    const void* keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
    const void* values[] = {font, color};
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, values, color ? 2 : 1, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef astr = CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(astr);
    CFRelease(astr);
    CFRelease(attrs);
    CFRelease(str);
    return line;
}

uint32_t to_bgra(uint32_t colorref) {
    uint32_t r = colorref & 0xFF, g = (colorref >> 8) & 0xFF, b = (colorref >> 16) & 0xFF;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

CGContextRef surface_context(Surface& s) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(s.pixels.data(), s.width, s.height, 8, s.width * 4, space,
                                             static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) |
                                                 static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst));
    CGColorSpaceRelease(space);
    return ctx;
}

}

int32_t cell_height(CTFontRef font) {
    return static_cast<int32_t>(std::ceil(CTFontGetAscent(font))) + static_cast<int32_t>(std::ceil(CTFontGetDescent(font)));
}

TextMetrics metrics(const FontSpec& spec) {
    CTFontRef font = font_for(spec);
    TextMetrics m;
    m.ascent = static_cast<int32_t>(std::ceil(CTFontGetAscent(font)));
    m.descent = static_cast<int32_t>(std::ceil(CTFontGetDescent(font)));
    m.height = m.ascent + m.descent;
    m.internal_leading = std::max<int32_t>(0, m.height - static_cast<int32_t>(std::lround(CTFontGetSize(font))));
    m.external_leading = static_cast<int32_t>(std::lround(CTFontGetLeading(font)));

    // 平均の文字幅は，Windows のダイアログの単位の計算と同じく英字 52 文字の平均 (四捨五入) にする
    int32_t w, h;
    text_extent(spec, u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", w, h);
    m.ave_char_width = (w / 26 + 1) / 2;
    m.max_char_width = static_cast<int32_t>(std::ceil(CTFontGetBoundingBox(font).size.width));
    return m;
}

void text_extent(const FontSpec& spec, std::u16string_view text, int32_t& width, int32_t& height) {
    CTFontRef font = font_for(spec);
    height = cell_height(font);
    if (text.empty()) {
        width = 0;
        return;
    }
    CTLineRef line = make_line(font, text, nullptr);
    width = static_cast<int32_t>(std::ceil(CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr)));
    CFRelease(line);
}

void draw_text(Surface& surface, const Rect& clip, int32_t x, int32_t y, uint32_t text_align, const FontSpec& spec,
               std::u16string_view text, uint32_t color, bool bk_opaque, uint32_t bk_color) {
    if (text.empty() || surface.width <= 0 || surface.height <= 0) return;
    CTFontRef font = font_for(spec);
    int32_t ascent = static_cast<int32_t>(std::ceil(CTFontGetAscent(font)));
    int32_t height = cell_height(font);

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGFloat comps[] = {(color & 0xFF) / 255.0, ((color >> 8) & 0xFF) / 255.0, ((color >> 16) & 0xFF) / 255.0, 1.0};
    CGColorRef cg_color = CGColorCreate(space, comps);
    CGColorSpaceRelease(space);
    CTLineRef line = make_line(font, text, cg_color);
    CGColorRelease(cg_color);
    auto width = static_cast<int32_t>(std::ceil(CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr)));

    // 揃え方 (TA_CENTER = 6, TA_RIGHT = 2, TA_BASELINE = 24, TA_BOTTOM = 8)
    if ((text_align & 6) == 6) x -= width / 2;
    else if (text_align & 2) x -= width;
    int32_t top = y;
    if ((text_align & 24) == 24) top = y - ascent;
    else if (text_align & 8) top = y - height;

    Rect bounds = clip.intersect({0, 0, surface.width, surface.height});
    if (bk_opaque) surface.fill(Rect{x, top, x + width, top + height}.intersect(bounds), to_bgra(bk_color));

    if (!bounds.empty()) {
        CGContextRef ctx = surface_context(surface);
        // CoreGraphics は左下が原点で上向きが正
        CGContextClipToRect(ctx, CGRectMake(bounds.left, surface.height - bounds.bottom, bounds.width(), bounds.height()));
        CGContextSetShouldAntialias(ctx, true);
        CGContextSetShouldSmoothFonts(ctx, false);
        CGContextSetTextPosition(ctx, x, surface.height - (top + ascent));
        CTLineDraw(line, ctx);
        if (spec.underline) {
            CGContextSetRGBFillColor(ctx, comps[0], comps[1], comps[2], 1);
            CGContextFillRect(ctx, CGRectMake(x, surface.height - (top + ascent + 2), width, 1));
        }
        CGContextRelease(ctx);
    }
    CFRelease(line);
}

void draw_image(Surface& surface, const Rect& clip, const Rect& dst, const uint32_t* pixels, int32_t width,
                int32_t height) {
    Rect bounds = clip.intersect({0, 0, surface.width, surface.height}).intersect(dst);
    if (bounds.empty() || width <= 0 || height <= 0) return;
    for (int32_t y = bounds.top; y < bounds.bottom; y++) {
        int32_t sy = static_cast<int32_t>(static_cast<int64_t>(y - dst.top) * height / dst.height());
        for (int32_t x = bounds.left; x < bounds.right; x++) {
            int32_t sx = static_cast<int32_t>(static_cast<int64_t>(x - dst.left) * width / dst.width());
            uint32_t src = pixels[static_cast<size_t>(sy) * width + sx];
            uint32_t a = src >> 24;
            if (a == 0) continue;
            uint32_t& d = surface.pixels[static_cast<size_t>(y) * surface.width + x];
            if (a == 255) {
                d = src | 0xFF000000u;
                continue;
            }
            uint32_t out = 0xFF000000u;
            for (int shift = 0; shift < 24; shift += 8) {
                uint32_t sc = (src >> shift) & 0xFF, dc = (d >> shift) & 0xFF;
                out |= ((sc * a + dc * (255 - a) + 127) / 255) << shift;
            }
            d = out;
        }
    }
}

}
