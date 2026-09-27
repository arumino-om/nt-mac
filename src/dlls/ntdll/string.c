#include "ntdll/ntdll.h"

/* ntdll がエクスポートするメモリ操作関数．コンパイラが生成する memcpy などの呼び出しもここで解決する */

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    uint8_t* d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = a;
    const uint8_t* y = b;
    for (; n; n--, x++, y++) {
        if (*x != *y) return *x - *y;
    }
    return 0;
}

size_t wcslen(const wchar_t* s) {
    const wchar_t* p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

size_t strlen(const char* s) {
    const char* p = s;
    while (*p) p++;
    return (size_t)(p - s);
}
