#pragma once
/*
 * ucrtbase.dll の内部ヘッダ．
 * mingw の CRT ヘッダ (stdio.h, stdlib.h など) や windows.h は，ここで定義する関数を dllimport 付きで宣言しているため使わない．
 * stddef.h などが間接的に読み込む宣言からも dllimport を外すため，_CRTIMP を空にしておく．
 */
#define _CRTIMP
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "shared/teb.h"

/* ---- kernel32 からインポートする関数 ---- */

#define WINAPI __stdcall
typedef int BOOL;
typedef uint32_t DWORD;
typedef void* HANDLE;

#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)
#define HEAP_ZERO_MEMORY  0x00000008

__declspec(dllimport) HANDLE WINAPI GetProcessHeap(void);
__declspec(dllimport) void* WINAPI HeapAlloc(HANDLE heap, DWORD flags, size_t size);
__declspec(dllimport) BOOL WINAPI HeapFree(HANDLE heap, DWORD flags, void* mem);
__declspec(dllimport) void* WINAPI HeapReAlloc(HANDLE heap, DWORD flags, void* mem, size_t size);
__declspec(dllimport) size_t WINAPI HeapSize(HANDLE heap, DWORD flags, const void* mem);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(DWORD std_handle);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE file, const void* buf, DWORD len, DWORD* written, void* overlapped);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE file, void* buf, DWORD len, DWORD* read, void* overlapped);
__declspec(dllimport) __attribute__((noreturn)) void WINAPI ExitProcess(unsigned exit_code);

static inline ntmac_teb* current_teb(void) {
    ntmac_teb* teb;
    __asm__("movq %%gs:0x30, %0" : "=r"(teb));
    return teb;
}

/* ---- CRT の定数 ---- */

#define EOF (-1)
#define EINVAL 22
#define ENOMEM 12
#define ERANGE 34

#define _IOFBF 0x0000
#define _IOLBF 0x0040
#define _IONBF 0x0004

/* __stdio_common_* の options */
#define CRT_PRINTF_STANDARD_SNPRINTF_BEHAVIOR 0x0002ULL

/* ---- 内部の関数 ---- */

typedef struct crt_file crt_file;

/* stdio.c */
void stdio_init(void);
void stdio_flush_all(void);
int file_write(crt_file* f, const char* data, size_t n);

/* printf.c: 書式を展開し，出力先に書き出す．戻り値は出力した文字数 (出力先のエラーなら -1) */
typedef struct print_sink {
    int (*write)(struct print_sink* sink, const char* data, size_t n);
    void* ctx;
} print_sink;
int format_output(print_sink* sink, const char* format, va_list args);

/* runtime.c */
void run_atexit_handlers(void);

/* heap.c */
void heap_init(void);

/* errno */
int* __cdecl _errno(void);

/* string.c */
void* __cdecl memcpy(void* dst, const void* src, size_t n);
void* __cdecl memset(void* dst, int c, size_t n);
void* __cdecl memmove(void* dst, const void* src, size_t n);
void* __cdecl memchr(const void* s, int c, size_t n);
size_t __cdecl strlen(const char* s);
size_t __cdecl strnlen(const char* s, size_t max);
void* __cdecl malloc(size_t size);
void __cdecl free(void* p);
void* __cdecl realloc(void* p, size_t size);

/* args.c */
int __cdecl _configure_narrow_argv(int mode);

/* args.c: UTF-16 → UTF-8 変換 (出力先が NULL なら必要なバイト数だけを返す．終端は含まない) */
size_t utf16_to_utf8(const uint16_t* src, size_t src_len, char* dst);
