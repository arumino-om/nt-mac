#include "ucrtbase_private.h"

/*
 * FILE の実装．現状は標準入出力の3つだけ．
 * 改行の変換 (テキストモードの "\n" → "\r\n") は行わない．
 */

#define FILE_BUFFER_SIZE 4096

struct crt_file {
    HANDLE handle;
    int mode;           /* _IOFBF / _IOLBF / _IONBF */
    int error;
    size_t len;         /* バッファに溜まっているバイト数 */
    char buf[FILE_BUFFER_SIZE];
};

static crt_file std_files[3];

void stdio_init(void) {
    static const DWORD handles[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (int i = 0; i < 3; i++) {
        std_files[i].handle = GetStdHandle(handles[i]);
        std_files[i].mode = i == 2 ? _IONBF : _IOLBF;
    }
}

static int write_all(crt_file* f, const char* data, size_t n) {
    while (n) {
        DWORD chunk = n > 0x7FFFFFFF ? 0x7FFFFFFF : (DWORD)n;
        DWORD written = 0;
        if (!WriteFile(f->handle, data, chunk, &written, NULL) || written == 0) {
            f->error = 1;
            return -1;
        }
        data += written;
        n -= written;
    }
    return 0;
}

static int flush_file(crt_file* f) {
    if (f->len == 0) return 0;
    int ret = write_all(f, f->buf, f->len);
    f->len = 0;
    return ret;
}

void stdio_flush_all(void) {
    for (int i = 0; i < 3; i++) flush_file(&std_files[i]);
}

int file_write(crt_file* f, const char* data, size_t n) {
    if (f->mode == _IONBF) {
        if (flush_file(f) < 0) return -1;
        return write_all(f, data, n);
    }

    const char* end = data + n;
    while (data < end) {
        size_t room = FILE_BUFFER_SIZE - f->len;
        size_t chunk = (size_t)(end - data) < room ? (size_t)(end - data) : room;
        memcpy(f->buf + f->len, data, chunk);
        f->len += chunk;
        data += chunk;
        if (f->len == FILE_BUFFER_SIZE && flush_file(f) < 0) return -1;
    }

    if (f->mode == _IOLBF && memchr(end - n, '\n', n)) return flush_file(f);
    return 0;
}

crt_file* __cdecl __acrt_iob_func(unsigned index) {
    return index < 3 ? &std_files[index] : NULL;
}

int __cdecl fflush(crt_file* f) {
    if (!f) {
        stdio_flush_all();
        return 0;
    }
    return flush_file(f) < 0 ? EOF : 0;
}

int __cdecl setvbuf(crt_file* f, char* buf, int mode, size_t size) {
    (void)buf;  /* 呼び出し側のバッファは使わず，内部のバッファを使う */
    (void)size;
    if (!f || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)) return -1;
    flush_file(f);
    f->mode = mode;
    return 0;
}

int __cdecl fputc(int c, crt_file* f) {
    char ch = (char)c;
    return file_write(f, &ch, 1) < 0 ? EOF : (unsigned char)ch;
}

int __cdecl putc(int c, crt_file* f) {
    return fputc(c, f);
}

int __cdecl putchar(int c) {
    return fputc(c, &std_files[1]);
}

int __cdecl fputs(const char* s, crt_file* f) {
    return file_write(f, s, strlen(s)) < 0 ? EOF : 0;
}

int __cdecl puts(const char* s) {
    crt_file* f = &std_files[1];
    if (file_write(f, s, strlen(s)) < 0 || file_write(f, "\n", 1) < 0) return EOF;
    return 0;
}

size_t __cdecl fwrite(const void* data, size_t size, size_t count, crt_file* f) {
    if (size == 0 || count == 0) return 0;
    return file_write(f, data, size * count) < 0 ? 0 : count;
}

int __cdecl ferror(crt_file* f) {
    return f->error;
}

/* ---- printf 系 ---- */

static int file_sink_write(print_sink* sink, const char* data, size_t n) {
    return file_write(sink->ctx, data, n);
}

int __cdecl __stdio_common_vfprintf(uint64_t options, crt_file* f, const char* format, void* locale, va_list args) {
    (void)options;
    (void)locale;
    if (!f || !format) return -1;
    print_sink sink = {file_sink_write, f};
    return format_output(&sink, format, args);
}

typedef struct buffer_ctx {
    char* buf;
    size_t cap;     /* 終端を含めて書き込める大きさ */
    size_t len;
} buffer_ctx;

static int buffer_sink_write(print_sink* sink, const char* data, size_t n) {
    buffer_ctx* b = sink->ctx;
    for (size_t i = 0; i < n; i++, b->len++) {
        if (b->len + 1 < b->cap) b->buf[b->len] = data[i];
    }
    return 0;
}

/* sprintf / snprintf 系．options に STANDARD_SNPRINTF_BEHAVIOR があれば C99 の snprintf と同じ戻り値にする */
int __cdecl __stdio_common_vsprintf(uint64_t options, char* buf, size_t count, const char* format, void* locale,
                                    va_list args) {
    (void)locale;
    if (!format || (!buf && count)) return -1;

    buffer_ctx ctx = {buf, count, 0};
    print_sink sink = {buffer_sink_write, &ctx};
    int len = format_output(&sink, format, args);
    if (len < 0) return -1;

    if (count) buf[(size_t)len < count ? (size_t)len : count - 1] = 0;
    if (options & CRT_PRINTF_STANDARD_SNPRINTF_BEHAVIOR) return len;
    return (size_t)len < count || (!buf && !count) ? len : -1;
}
