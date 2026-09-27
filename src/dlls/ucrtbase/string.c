#include "ucrtbase_private.h"

void* __cdecl memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void* __cdecl memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

void* __cdecl memset(void* dst, int c, size_t n) {
    uint8_t* d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

int __cdecl memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = a;
    const uint8_t* y = b;
    for (; n; n--, x++, y++) {
        if (*x != *y) return *x - *y;
    }
    return 0;
}

void* __cdecl memchr(const void* s, int c, size_t n) {
    const uint8_t* p = s;
    for (; n; n--, p++) {
        if (*p == (uint8_t)c) return (void*)p;
    }
    return NULL;
}

size_t __cdecl strlen(const char* s) {
    const char* p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

size_t __cdecl strnlen(const char* s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

int __cdecl strcmp(const char* a, const char* b) {
    while (*a && *a == *b) a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int __cdecl strncmp(const char* a, const char* b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}

char* __cdecl strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

char* __cdecl strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

char* __cdecl strcat(char* dst, const char* src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

char* __cdecl strchr(const char* s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char*)s;
        if (!*s) return NULL;
    }
}

char* __cdecl strrchr(const char* s, int c) {
    const char* last = NULL;
    for (;; s++) {
        if (*s == (char)c) last = s;
        if (!*s) return (char*)last;
    }
}

char* __cdecl strstr(const char* haystack, const char* needle) {
    size_t n = strlen(needle);
    for (; *haystack; haystack++) {
        if (!strncmp(haystack, needle, n)) return (char*)haystack;
    }
    return n ? NULL : (char*)haystack;
}

char* __cdecl _strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ---- ワイド文字 (Windows の wchar_t は UTF-16 の 16 ビット) ---- */

size_t __cdecl wcslen(const uint16_t* s) {
    const uint16_t* p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

size_t __cdecl wcsnlen(const uint16_t* s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

int __cdecl wcscmp(const uint16_t* a, const uint16_t* b) {
    while (*a && *a == *b) a++, b++;
    return (int)*a - (int)*b;
}

int __cdecl wcsncmp(const uint16_t* a, const uint16_t* b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a) return (int)*a - (int)*b;
    }
    return 0;
}

uint16_t* __cdecl wcscpy(uint16_t* dst, const uint16_t* src) {
    uint16_t* d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

uint16_t* __cdecl wcscat(uint16_t* dst, const uint16_t* src) {
    wcscpy(dst + wcslen(dst), src);
    return dst;
}

uint16_t* __cdecl wcschr(const uint16_t* s, uint16_t c) {
    for (;; s++) {
        if (*s == c) return (uint16_t*)s;
        if (!*s) return NULL;
    }
}
