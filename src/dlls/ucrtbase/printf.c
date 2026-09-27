#include "ucrtbase_private.h"

/*
 * printf の書式の展開．
 *
 * 対応: フラグ (- + 空白 # 0)，幅と精度 (* を含む)，長さ修飾子 (hh h l ll L j z t w I I32 I64)，
 *       変換 (d i u o x X c C s S p f F e E g G a A %)
 * %n は UCRT の既定と同様に無効 (引数は読み飛ばす)．
 *
 * 浮動小数点の10進変換は long double (仮数部64ビット) で行うため，有効数字20桁より先は 0 になり，
 * 17桁を超える部分の丸めは正確でない場合がある．
 */

enum length_modifier { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIG_L, LEN_W, LEN_I, LEN_I32, LEN_I64 };

typedef struct spec {
    int left, plus, space, alt, zero;
    int width;      /* 指定なしは 0 */
    int precision;  /* 指定なしは -1 */
    int length;
    char conv;
} spec;

typedef struct output {
    print_sink* sink;
    int count;
    int error;
} output;

static void emit(output* o, const char* s, size_t n) {
    if (o->error || n == 0) return;
    if (o->sink->write(o->sink, s, n) < 0) {
        o->error = 1;
        return;
    }
    o->count += (int)n;
}

static void emit_repeat(output* o, char c, int n) {
    char buf[32];
    memset(buf, c, sizeof(buf));
    while (n > 0) {
        int k = n > 32 ? 32 : n;
        emit(o, buf, (size_t)k);
        n -= k;
    }
}

/* [空白] 接頭辞 [0埋め] ゼロ 本体 [空白] の順に出力する */
static void emit_field(output* o, const spec* s, const char* prefix, size_t prefix_len, int zeros,
                       const char* body, size_t body_len) {
    int total = (int)(prefix_len + body_len) + zeros;
    int padding = s->width > total ? s->width - total : 0;
    if (!s->left && !s->zero) emit_repeat(o, ' ', padding);
    emit(o, prefix, prefix_len);
    if (!s->left && s->zero) emit_repeat(o, '0', padding);
    emit_repeat(o, '0', zeros);
    emit(o, body, body_len);
    if (s->left) emit_repeat(o, ' ', padding);
}

static size_t sign_prefix(const spec* s, int negative, char* prefix) {
    if (negative) prefix[0] = '-';
    else if (s->plus) prefix[0] = '+';
    else if (s->space) prefix[0] = ' ';
    else return 0;
    return 1;
}

/* ---- 整数 ---- */

static void format_integer(output* o, spec* s, uint64_t value, int negative) {
    int base = 10;
    const char* digits = "0123456789abcdef";
    switch (s->conv) {
    case 'o': base = 8; break;
    case 'x': base = 16; break;
    case 'X': case 'p': base = 16; digits = "0123456789ABCDEF"; break;
    }

    char buf[24];
    char* end = buf + sizeof(buf);
    char* p = end;
    for (uint64_t v = value; v; v /= (uint64_t)base) *--p = digits[v % (uint64_t)base];
    int len = (int)(end - p);

    int zeros = 0;
    if (s->precision >= 0) {
        s->zero = 0;                    /* 精度の指定があれば 0 フラグは無視される */
        if (len < s->precision) zeros = s->precision - len;
    } else if (len == 0) {
        zeros = 1;                      /* 値が 0 */
    }

    char prefix[3];
    size_t prefix_len = 0;
    if (s->conv == 'd' || s->conv == 'i') prefix_len = sign_prefix(s, negative, prefix);
    if (s->alt && value && base == 16) {
        prefix[prefix_len++] = '0';
        prefix[prefix_len++] = s->conv == 'X' ? 'X' : 'x';
    }
    if (s->alt && base == 8 && zeros == 0 && len > 0 && *p != '0') zeros = 1;

    emit_field(o, s, prefix, prefix_len, zeros, p, (size_t)len);
}

/* ---- 浮動小数点 ---- */

#define MAX_DIGITS 20   /* long double で意味のある桁数 */

/* 値 = d[0].d[1]d[2]... × 10^exp．n 桁より先は 0 */
typedef struct decimal {
    uint8_t d[MAX_DIGITS];
    int n;
    int exp;
} decimal;

static long double pow10l(int n) {
    long double result = 1, base = 10;
    for (unsigned k = (unsigned)(n < 0 ? -n : n); k; k >>= 1, base *= base) {
        if (k & 1) result *= base;
    }
    return n < 0 ? 1 / result : result;
}

/* x87 の拡張倍精度 (long double) の2進指数 */
static int binary_exponent(long double x) {
    struct {
        uint64_t mantissa;
        uint16_t sign_exponent;
    } bits;
    memcpy(&bits, &x, 10);
    return (bits.sign_exponent & 0x7FFF) - 16383;
}

/* x (> 0) を [1, 10) に正規化し，10 の指数を返す */
static int normalize(long double* x) {
    int e = (int)(binary_exponent(*x) * 0.30102999566398119521L);
    *x *= pow10l(-e);
    while (*x >= 10) *x /= 10, e++;
    while (*x < 1) *x *= 10, e--;
    return e;
}

/* x (>= 0) を有効数字 want 桁に丸めて10進数にする */
static void to_decimal(long double x, int want, decimal* dec) {
    memset(dec, 0, sizeof(*dec));
    if (x == 0) return;

    dec->exp = normalize(&x);
    int gen = want < MAX_DIGITS ? want : MAX_DIGITS;
    for (int i = 0; i < gen; i++) {
        int digit = (int)x;
        dec->d[i] = (uint8_t)digit;
        x = (x - digit) * 10;
    }
    dec->n = gen;

    /* 次の桁で四捨五入する (want 桁目が有効な範囲内のときのみ) */
    if (want <= MAX_DIGITS && (int)x >= 5) {
        int i = gen - 1;
        for (; i >= 0 && dec->d[i] == 9; i--) dec->d[i] = 0;
        if (i >= 0) {
            dec->d[i]++;
        } else {
            dec->d[0] = 1;  /* 9.99... → 10.0 */
            dec->exp++;
        }
    }
}

static char digit_at(const decimal* dec, int i) {
    return (char)('0' + (i >= 0 && i < dec->n ? dec->d[i] : 0));
}

/* 固定小数点表記 (%f) を buf に書き，長さを返す */
static size_t layout_fixed(const decimal* dec, int precision, int alt, char* buf) {
    size_t n = 0;
    for (int q = dec->exp > 0 ? dec->exp : 0; q >= 0; q--) buf[n++] = digit_at(dec, dec->exp - q);
    if (precision > 0 || alt) buf[n++] = '.';
    for (int q = -1; q >= -precision; q--) buf[n++] = digit_at(dec, dec->exp - q);
    return n;
}

/* 指数表記 (%e) を buf に書き，長さを返す */
static size_t layout_exponent(const decimal* dec, int precision, int alt, int upper, char* buf) {
    size_t n = 0;
    buf[n++] = digit_at(dec, 0);
    if (precision > 0 || alt) buf[n++] = '.';
    for (int i = 1; i <= precision; i++) buf[n++] = digit_at(dec, i);

    int e = dec->n ? dec->exp : 0;
    buf[n++] = upper ? 'E' : 'e';
    buf[n++] = e < 0 ? '-' : '+';
    if (e < 0) e = -e;
    char tmp[8];
    int k = 0;
    do tmp[k++] = (char)('0' + e % 10); while ((e /= 10));
    if (k < 2) tmp[k++] = '0';  /* 指数は2桁以上 (UCRT と同じ) */
    while (k) buf[n++] = tmp[--k];
    return n;
}

/* %g で，小数部の末尾の 0 (と不要な小数点) を取り除く */
static size_t strip_trailing_zeros(char* buf, size_t n) {
    size_t exp_pos = n;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == 'e' || buf[i] == 'E') exp_pos = i;
    }

    size_t dot = exp_pos;
    for (size_t i = 0; i < exp_pos; i++) {
        if (buf[i] == '.') dot = i;
    }
    if (dot == exp_pos) return n;

    size_t end = exp_pos;
    while (end > dot + 1 && buf[end - 1] == '0') end--;
    if (end == dot + 1) end = dot;
    memmove(buf + end, buf + exp_pos, n - exp_pos);
    return end + (n - exp_pos);
}

static size_t layout_hex(uint64_t bits, int precision, int upper, char* buf) {
    const char* hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int biased = (int)((bits >> 52) & 0x7FF);
    uint64_t mant = bits & ((1ULL << 52) - 1);
    int lead = biased ? 1 : 0;
    int exp = biased ? biased - 1023 : (mant ? -1022 : 0);

    if (precision < 0) precision = 13;
    if (precision < 13) {
        /* 仮数部を precision 桁の16進数に丸める */
        int drop = (13 - precision) * 4;
        uint64_t half = 1ULL << (drop - 1);
        uint64_t rest = mant & ((1ULL << drop) - 1);
        mant >>= drop;
        if (rest > half || (rest == half && (mant & 1))) mant++;
        if (mant >> (precision * 4)) {  /* 繰り上がり */
            mant = 0;
            lead++;
        }
        mant <<= drop;
    }

    size_t n = 0;
    buf[n++] = '0';
    buf[n++] = upper ? 'X' : 'x';
    buf[n++] = hex[lead];
    if (precision > 0) buf[n++] = '.';
    for (int i = 0; i < precision; i++) buf[n++] = i < 13 ? hex[(mant >> (48 - i * 4)) & 0xF] : '0';
    buf[n++] = upper ? 'P' : 'p';
    buf[n++] = exp < 0 ? '-' : '+';
    if (exp < 0) exp = -exp;
    char tmp[8];
    int k = 0;
    do tmp[k++] = (char)('0' + exp % 10); while ((exp /= 10));
    while (k) buf[n++] = tmp[--k];
    return n;
}

static void format_float(output* o, spec* s, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    int negative = (int)(bits >> 63);
    int upper = s->conv >= 'A' && s->conv <= 'Z';
    char lower = (char)(s->conv | 0x20);

    char prefix[1];
    size_t prefix_len = sign_prefix(s, negative, prefix);

    if (((bits >> 52) & 0x7FF) == 0x7FF) {
        const char* body = (bits & ((1ULL << 52) - 1)) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        s->zero = 0;
        emit_field(o, s, prefix, prefix_len, 0, body, 3);
        return;
    }

    long double x = negative ? -(long double)value : (long double)value;
    int precision = s->precision;
    if (precision < 0) precision = lower == 'a' ? -1 : 6;

    /* 本体の最大長を見積もって領域を確保する (%f の整数部は最大 309 桁) */
    size_t cap = (size_t)(precision > 0 ? precision : 0) + 340;
    char stack_buf[512];
    char* buf = cap <= sizeof(stack_buf) ? stack_buf : malloc(cap);
    if (!buf) {
        o->error = 1;
        return;
    }

    decimal dec;
    size_t n;
    switch (lower) {
    case 'e':
        to_decimal(x, precision + 1, &dec);
        n = layout_exponent(&dec, precision, s->alt, upper, buf);
        break;
    case 'f': {
        int e = 0;
        if (x != 0) {
            long double t = x;
            e = normalize(&t);
        }
        int want = e + 1 + precision;
        if (want > 0) {
            to_decimal(x, want, &dec);
        } else {
            /* 小数点以下 precision 桁より小さい値: 0 か，最下位の桁への切り上げになる */
            to_decimal(x, 1, &dec);
            int round_up = want == 0 && dec.d[0] >= 5;
            memset(&dec, 0, sizeof(dec));
            if (round_up) {
                dec.d[0] = 1;
                dec.n = 1;
                dec.exp = -precision;
            }
        }
        n = layout_fixed(&dec, precision, s->alt, buf);
        break;
    }
    case 'g': {
        int p = precision == 0 ? 1 : precision;
        to_decimal(x, p, &dec);
        int e = dec.n ? dec.exp : 0;
        if (e < p && e >= -4) {
            n = layout_fixed(&dec, p - 1 - e, s->alt, buf);
        } else {
            n = layout_exponent(&dec, p - 1, s->alt, upper, buf);
        }
        if (!s->alt) n = strip_trailing_zeros(buf, n);
        break;
    }
    default: /* 'a' */
        n = layout_hex(bits, precision, upper, buf);
        break;
    }

    emit_field(o, s, prefix, prefix_len, 0, buf, n);
    if (buf != stack_buf) free(buf);
}

/* ---- 文字と文字列 ---- */

static void format_string(output* o, spec* s, const char* str, size_t len) {
    if (s->precision >= 0 && (size_t)s->precision < len) len = (size_t)s->precision;
    s->zero = 0;
    emit_field(o, s, NULL, 0, 0, str, len);
}

static void format_wide_string(output* o, spec* s, const uint16_t* str, size_t len) {
    if (s->precision >= 0 && (size_t)s->precision < len) len = (size_t)s->precision;
    size_t n = utf16_to_utf8(str, len, NULL);
    char stack_buf[256];
    char* buf = n <= sizeof(stack_buf) ? stack_buf : malloc(n);
    if (!buf) {
        o->error = 1;
        return;
    }
    utf16_to_utf8(str, len, buf);
    s->precision = -1;
    format_string(o, s, buf, n);
    if (buf != stack_buf) free(buf);
}

static int is_wide(const spec* s) {
    if (s->conv == 'C' || s->conv == 'S') return s->length != LEN_H;
    return s->length == LEN_L || s->length == LEN_W;
}

/* ---- 書式の解析 ---- */

static int parse_int(const char** p) {
    int v = 0;
    while (**p >= '0' && **p <= '9') {
        if (v < 100000000) v = v * 10 + (**p - '0');
        (*p)++;
    }
    return v;
}

static int parse_length(const char** p) {
    const char* f = *p;
    int len = LEN_NONE;
    switch (*f) {
    case 'h': len = f[1] == 'h' ? (f++, LEN_HH) : LEN_H; f++; break;
    case 'l': len = f[1] == 'l' ? (f++, LEN_LL) : LEN_L; f++; break;
    case 'L': len = LEN_BIG_L; f++; break;
    case 'j': len = LEN_J; f++; break;
    case 'z': len = LEN_Z; f++; break;
    case 't': len = LEN_T; f++; break;
    case 'w': len = LEN_W; f++; break;
    case 'I':
        if (f[1] == '6' && f[2] == '4') { len = LEN_I64; f += 3; }
        else if (f[1] == '3' && f[2] == '2') { len = LEN_I32; f += 3; }
        else { len = LEN_I; f++; }
        break;
    }
    *p = f;
    return len;
}

/* va_list は Windows x64 ではポインタなので，関数をまたいで va_arg で読み進められるよう参照で渡す */
#define ARGS (*ap)

static uint64_t fetch_unsigned(const spec* s, va_list* ap) {
    switch (s->length) {
    case LEN_HH: return (unsigned char)va_arg(ARGS, int);
    case LEN_H:  return (unsigned short)va_arg(ARGS, int);
    case LEN_LL: case LEN_J: case LEN_Z: case LEN_T: case LEN_I: case LEN_I64:
        return va_arg(ARGS, uint64_t);
    default:     return va_arg(ARGS, unsigned int); /* long も 32 ビット */
    }
}

static int64_t fetch_signed(const spec* s, va_list* ap) {
    switch (s->length) {
    case LEN_HH: return (signed char)va_arg(ARGS, int);
    case LEN_H:  return (short)va_arg(ARGS, int);
    case LEN_LL: case LEN_J: case LEN_Z: case LEN_T: case LEN_I: case LEN_I64:
        return va_arg(ARGS, int64_t);
    default:     return va_arg(ARGS, int);
    }
}

int format_output(print_sink* sink, const char* format, va_list args) {
    output o = {sink, 0, 0};
    va_list ap_copy;
    va_copy(ap_copy, args);
    va_list* ap = &ap_copy;

    for (const char* f = format; *f && !o.error;) {
        if (*f != '%') {
            const char* start = f;
            while (*f && *f != '%') f++;
            emit(&o, start, (size_t)(f - start));
            continue;
        }
        const char* conv_start = f++;

        spec s = {0};
        s.precision = -1;
        for (;; f++) {
            if (*f == '-') s.left = 1;
            else if (*f == '+') s.plus = 1;
            else if (*f == ' ') s.space = 1;
            else if (*f == '#') s.alt = 1;
            else if (*f == '0') s.zero = 1;
            else break;
        }

        if (*f == '*') {
            s.width = va_arg(ARGS, int);
            if (s.width < 0) {
                s.left = 1;
                s.width = -s.width;
            }
            f++;
        } else {
            s.width = parse_int(&f);
        }

        if (*f == '.') {
            f++;
            if (*f == '*') {
                s.precision = va_arg(ARGS, int);
                if (s.precision < 0) s.precision = -1;
                f++;
            } else {
                s.precision = parse_int(&f);
            }
        }

        s.length = parse_length(&f);
        if (s.left) s.zero = 0;
        s.conv = *f;
        if (!s.conv) break;
        f++;

        switch (s.conv) {
        case 'd': case 'i': {
            int64_t v = fetch_signed(&s, ap);
            format_integer(&o, &s, v < 0 ? 0 - (uint64_t)v : (uint64_t)v, v < 0);
            break;
        }
        case 'u': case 'o': case 'x': case 'X':
            format_integer(&o, &s, fetch_unsigned(&s, ap), 0);
            break;
        case 'p':
            s.precision = 16;   /* Windows では 0x を付けず，16桁の大文字で表示する */
            s.alt = 0;
            format_integer(&o, &s, (uint64_t)va_arg(ARGS, void*), 0);
            break;
        case 'c': case 'C':
            if (is_wide(&s)) {
                uint16_t wc = (uint16_t)va_arg(ARGS, int);
                format_wide_string(&o, &s, &wc, 1);
            } else {
                char c = (char)va_arg(ARGS, int);
                s.precision = -1;
                format_string(&o, &s, &c, 1);
            }
            break;
        case 's': case 'S':
            if (is_wide(&s)) {
                const uint16_t* ws = va_arg(ARGS, const uint16_t*);
                if (!ws) {
                    format_string(&o, &s, "(null)", 6);
                } else {
                    size_t len = 0;
                    while (ws[len] && (s.precision < 0 || len < (size_t)s.precision)) len++;
                    format_wide_string(&o, &s, ws, len);
                }
            } else {
                const char* str = va_arg(ARGS, const char*);
                if (!str) str = "(null)";
                format_string(&o, &s, str, s.precision >= 0 ? strnlen(str, (size_t)s.precision) : strlen(str));
            }
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            format_float(&o, &s, va_arg(ARGS, double)); /* long double も double と同じ */
            break;
        case 'n':
            (void)va_arg(ARGS, void*);  /* UCRT の既定と同様に書き込まない */
            break;
        case '%':
            emit(&o, "%", 1);
            break;
        default:
            /* 不明な変換はそのまま出力する */
            emit(&o, conv_start, (size_t)(f - conv_start));
            break;
        }
    }

    va_end(ap_copy);
    return o.error ? -1 : o.count;
}
