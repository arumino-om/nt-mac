#include "kernel32_private.h"

/*
 * コードページの変換．対応するのは UTF-8 (CP_ACP / CP_OEMCP / CP_THREAD_ACP も UTF-8 として扱う) と，
 * Latin-1 系 (20127 US-ASCII, 28591 ISO-8859-1, 1252 は近似として Latin-1)．
 */

enum encoding { ENC_UTF8, ENC_LATIN1, ENC_INVALID };

static enum encoding encoding_of(UINT cp) {
    switch (cp) {
    case CP_ACP: case CP_OEMCP: case CP_MACCP: case CP_THREAD_ACP: case CP_UTF8:
        return ENC_UTF8;
    case 20127: case 28591: case 1252:
        return ENC_LATIN1;
    default:
        return ENC_INVALID;
    }
}

UINT WINAPI GetACP(void) {
    return NTMAC_ANSI_CODE_PAGE;
}

UINT WINAPI GetOEMCP(void) {
    return NTMAC_ANSI_CODE_PAGE;
}

UINT WINAPI GetConsoleCP(void) {
    return NTMAC_ANSI_CODE_PAGE;
}

UINT WINAPI GetConsoleOutputCP(void) {
    return NTMAC_ANSI_CODE_PAGE;
}

BOOL WINAPI IsValidCodePage(UINT CodePage) {
    return CodePage != CP_ACP && CodePage != CP_OEMCP && encoding_of(CodePage) != ENC_INVALID;
}

BOOL WINAPI GetCPInfo(UINT CodePage, LPCPINFO lpCPInfo) {
    enum encoding enc = encoding_of(CodePage);
    if (enc == ENC_INVALID || !lpCPInfo) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memset(lpCPInfo, 0, sizeof(*lpCPInfo));
    lpCPInfo->MaxCharSize = enc == ENC_UTF8 ? 4 : 1;
    lpCPInfo->DefaultChar[0] = '?';
    return TRUE;
}

/* UTF-8 を1文字読む．不正なら *invalid を立てて U+FFFD を返す */
static uint32_t decode_utf8(const unsigned char** p, const unsigned char* end, BOOL* invalid) {
    const unsigned char* s = *p;
    uint32_t c = *s++;
    int n = 0;
    uint32_t min = 0;
    if (c < 0x80) {
        *p = s;
        return c;
    } else if ((c & 0xE0) == 0xC0) { n = 1; c &= 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0)   { n = 2; c &= 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0)   { n = 3; c &= 0x07; min = 0x10000; }
    else { *p = s; *invalid = TRUE; return 0xFFFD; }

    for (int i = 0; i < n; i++) {
        if (s >= end || (*s & 0xC0) != 0x80) {
            *p = s;
            *invalid = TRUE;
            return 0xFFFD;
        }
        c = (c << 6) | (*s++ & 0x3F);
    }
    *p = s;
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) {
        *invalid = TRUE;
        return 0xFFFD;
    }
    return c;
}

int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte,
                               LPWSTR lpWideCharStr, int cchWideChar) {
    enum encoding enc = encoding_of(CodePage);
    if (enc == ENC_INVALID || !lpMultiByteStr || cbMultiByte == 0 || cchWideChar < 0 ||
        (cchWideChar && !lpWideCharStr)) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return 0;
    }

    const unsigned char* p = (const unsigned char*)lpMultiByteStr;
    const unsigned char* end = p + (cbMultiByte < 0 ? strlen(lpMultiByteStr) + 1 : (size_t)cbMultiByte);
    int n = 0;
    BOOL invalid = FALSE;

    while (p < end) {
        uint32_t c = enc == ENC_UTF8 ? decode_utf8(&p, end, &invalid) : *p++;
        if (invalid && (dwFlags & MB_ERR_INVALID_CHARS)) {
            RtlSetLastWin32Error(ERROR_NO_UNICODE_TRANSLATION);
            return 0;
        }

        WCHAR units[2];
        int count = 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            units[0] = (WCHAR)(0xD800 | (c >> 10));
            units[1] = (WCHAR)(0xDC00 | (c & 0x3FF));
            count = 2;
        } else {
            units[0] = (WCHAR)c;
        }

        if (cchWideChar) {
            if (n + count > cchWideChar) {
                RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            for (int i = 0; i < count; i++) lpWideCharStr[n + i] = units[i];
        }
        n += count;
    }
    return n;
}

int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar,
                               LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar) {
    (void)dwFlags;
    enum encoding enc = encoding_of(CodePage);
    if (enc == ENC_INVALID || !lpWideCharStr || cchWideChar == 0 || cbMultiByte < 0 ||
        (cbMultiByte && !lpMultiByteStr) ||
        (enc == ENC_UTF8 && (lpDefaultChar || lpUsedDefaultChar))) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return 0;
    }

    size_t len = cchWideChar < 0 ? wcslen(lpWideCharStr) + 1 : (size_t)cchWideChar;
    char default_char = lpDefaultChar ? *lpDefaultChar : '?';
    if (lpUsedDefaultChar) *lpUsedDefaultChar = FALSE;
    int n = 0;

    for (size_t i = 0; i < len; i++) {
        uint32_t c = lpWideCharStr[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < len && lpWideCharStr[i + 1] >= 0xDC00 && lpWideCharStr[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (lpWideCharStr[i + 1] - 0xDC00);
            i++;
        } else if (c >= 0xD800 && c < 0xE000) {
            c = 0xFFFD;
        }

        unsigned char bytes[4];
        int count;
        if (enc == ENC_LATIN1) {
            if (c > 0xFF) {
                c = (unsigned char)default_char;
                if (lpUsedDefaultChar) *lpUsedDefaultChar = TRUE;
            }
            bytes[0] = (unsigned char)c;
            count = 1;
        } else if (c < 0x80) {
            bytes[0] = (unsigned char)c; count = 1;
        } else if (c < 0x800) {
            bytes[0] = (unsigned char)(0xC0 | (c >> 6)); bytes[1] = (unsigned char)(0x80 | (c & 0x3F)); count = 2;
        } else if (c < 0x10000) {
            bytes[0] = (unsigned char)(0xE0 | (c >> 12)); bytes[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
            bytes[2] = (unsigned char)(0x80 | (c & 0x3F)); count = 3;
        } else {
            bytes[0] = (unsigned char)(0xF0 | (c >> 18)); bytes[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
            bytes[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3F)); bytes[3] = (unsigned char)(0x80 | (c & 0x3F));
            count = 4;
        }

        if (cbMultiByte) {
            if (n + count > cbMultiByte) {
                RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            memcpy(lpMultiByteStr + n, bytes, (size_t)count);
        }
        n += count;
    }
    return n;
}

BOOL ansi_to_wide(LPCSTR src, LPWSTR dst, int dst_len) {
    return MultiByteToWideChar(CP_ACP, 0, src, -1, dst, dst_len) > 0;
}
