#include "ucrtbase_private.h"

/* コマンドラインと環境変数は PEB の UTF-16 から UTF-8 に変換して使う (ANSI コードページは UTF-8 とみなす) */

static int crt_argc;
static char** crt_argv;
static char** crt_environ;

size_t utf16_to_utf8(const uint16_t* src, size_t src_len, char* dst) {
    size_t n = 0;
    for (size_t i = 0; i < src_len; i++) {
        uint32_t cp = src[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < src_len && src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD; /* 対になっていないサロゲート */
        }

        uint8_t buf[4];
        size_t len;
        if (cp < 0x80)         { buf[0] = (uint8_t)cp; len = 1; }
        else if (cp < 0x800)   { buf[0] = 0xC0 | (cp >> 6);  buf[1] = 0x80 | (cp & 0x3F); len = 2; }
        else if (cp < 0x10000) { buf[0] = 0xE0 | (cp >> 12); buf[1] = 0x80 | ((cp >> 6) & 0x3F);
                                 buf[2] = 0x80 | (cp & 0x3F); len = 3; }
        else                   { buf[0] = 0xF0 | (cp >> 18); buf[1] = 0x80 | ((cp >> 12) & 0x3F);
                                 buf[2] = 0x80 | ((cp >> 6) & 0x3F); buf[3] = 0x80 | (cp & 0x3F); len = 4; }
        if (dst) memcpy(dst + n, buf, len);
        n += len;
    }
    return n;
}

static char* to_utf8(const uint16_t* src, size_t src_len) {
    size_t n = utf16_to_utf8(src, src_len, NULL);
    char* s = malloc(n + 1);
    if (!s) return NULL;
    utf16_to_utf8(src, src_len, s);
    s[n] = 0;
    return s;
}

/*
 * コマンドラインを Windows の規則で分割する．args が NULL なら数と必要なバイト数だけを数える．
 *   - 空白で区切る．"..." の中の空白は区切らない
 *   - 2n 個の \ と " → n 個の \ (" は引用の開始/終了)，2n+1 個の \ と " → n 個の \ と "
 *   - 引用の中の "" → "
 *   - 先頭 (プログラム名) だけは \ を特別扱いせず，" で囲まれていればその中身をそのまま使う
 */
static void split_command_line(const char* line, int* argc, char** args, char* buf, size_t* buf_size) {
    const char* p = line;
    size_t out = 0;
    int count = 0;

/* 引数は buf が NULL のとき評価されないので，副作用のある式を渡さないこと */
#define PUT(c) do { if (buf) buf[out] = (c); out++; } while (0)

    /* プログラム名 */
    if (args) args[count] = buf + out;
    count++;
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) PUT(*p);
        if (*p == '"') p++;
    } else {
        for (; *p && *p != ' ' && *p != '\t'; p++) PUT(*p);
    }
    PUT(0);

    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (args) args[count] = buf + out;
        count++;
        int quoted = 0;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '\\') {
                size_t backslashes = 0;
                while (*p == '\\') backslashes++, p++;
                if (*p == '"') {
                    for (size_t i = 0; i < backslashes / 2; i++) PUT('\\');
                    if (backslashes % 2) {
                        PUT('"');
                        p++;
                    }
                } else {
                    for (size_t i = 0; i < backslashes; i++) PUT('\\');
                }
            } else if (*p == '"') {
                if (quoted && p[1] == '"') {
                    PUT('"');
                    p += 2;
                } else {
                    quoted = !quoted;
                    p++;
                }
            } else {
                PUT(*p);
                p++;
            }
        }
        PUT(0);
    }
    if (args) args[count] = NULL;

#undef PUT
    *argc = count;
    *buf_size = out;
}

int __cdecl _configure_narrow_argv(int mode) {
    (void)mode; /* ワイルドカードの展開 (_crt_argv_expanded_arguments) は未対応 */
    if (crt_argv) return 0;

    ntmac_unicode_string* cmd = &current_teb()->ProcessEnvironmentBlock->ProcessParameters->CommandLine;
    char* line = to_utf8(cmd->Buffer, cmd->Length / sizeof(uint16_t));
    if (!line) return ENOMEM;

    int argc;
    size_t size;
    split_command_line(line, &argc, NULL, NULL, &size);

    /* argv の配列と文字列を1つの領域にまとめる */
    char** argv = malloc((size_t)(argc + 1) * sizeof(char*) + size);
    if (!argv) {
        free(line);
        return ENOMEM;
    }
    split_command_line(line, &argc, argv, (char*)(argv + argc + 1), &size);
    free(line);

    crt_argc = argc;
    crt_argv = argv;
    return 0;
}

int __cdecl _initialize_narrow_environment(void) {
    if (crt_environ) return 0;

    const uint16_t* block = current_teb()->ProcessEnvironmentBlock->ProcessParameters->Environment;
    size_t count = 0;
    if (block) {
        for (const uint16_t* p = block; *p; count++) {
            while (*p) p++;
            p++;
        }
    }

    char** env = malloc((count + 1) * sizeof(char*));
    if (!env) return ENOMEM;

    size_t i = 0;
    for (const uint16_t* p = block; p && *p; i++) {
        const uint16_t* start = p;
        while (*p) p++;
        env[i] = to_utf8(start, (size_t)(p - start));
        if (!env[i]) return ENOMEM;
        p++;
    }
    env[i] = NULL;

    crt_environ = env;
    return 0;
}

int* __cdecl __p___argc(void) {
    if (!crt_argv) _configure_narrow_argv(1);
    return &crt_argc;
}

char*** __cdecl __p___argv(void) {
    if (!crt_argv) _configure_narrow_argv(1);
    return &crt_argv;
}

char*** __cdecl __p__environ(void) {
    if (!crt_environ) _initialize_narrow_environment();
    return &crt_environ;
}

static int ascii_lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/* Windows の環境変数名は大文字と小文字を区別しない */
char* __cdecl getenv(const char* name) {
    if (!name || !*name) return NULL;
    char** env = *__p__environ();
    size_t len = strlen(name);

    for (; env && *env; env++) {
        const char* e = *env;
        size_t i = 0;
        while (i < len && e[i] && ascii_lower((unsigned char)e[i]) == ascii_lower((unsigned char)name[i])) i++;
        if (i == len && e[i] == '=') return (char*)e + len + 1;
    }
    return NULL;
}

/* ---- ワイド文字版 (wmain / wWinMain) ---- */

static int crt_wargc;
static uint16_t** crt_wargv;
static uint16_t** crt_wenviron;

static uint16_t* wide_dup(const uint16_t* s, size_t len) {
    uint16_t* d = malloc((len + 1) * sizeof(uint16_t));
    if (!d) return NULL;
    memcpy(d, s, len * sizeof(uint16_t));
    d[len] = 0;
    return d;
}

int __cdecl _configure_wide_argv(int mode) {
    (void)mode;
    if (crt_wargv) return 0;
    /* 狭い文字の argv を作り，それを UTF-16 に戻す (分割の規則を1か所にまとめるため) */
    if (_configure_narrow_argv(mode) != 0) return ENOMEM;

    uint16_t** wargv = malloc((size_t)(crt_argc + 1) * sizeof(uint16_t*));
    if (!wargv) return ENOMEM;
    for (int i = 0; i < crt_argc; i++) {
        const unsigned char* s = (const unsigned char*)crt_argv[i];
        size_t n = strlen(crt_argv[i]);
        uint16_t* w = malloc((n + 1) * sizeof(uint16_t));
        if (!w) return ENOMEM;
        size_t k = 0;
        for (size_t j = 0; j < n;) {
            uint32_t c = s[j];
            size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
            c = len == 1 ? c : len == 2 ? c & 0x1F : len == 3 ? c & 0x0F : c & 0x07;
            for (size_t t = 1; t < len && j + t < n; t++) c = (c << 6) | (s[j + t] & 0x3F);
            j += len;
            if (c >= 0x10000) {
                c -= 0x10000;
                w[k++] = (uint16_t)(0xD800 | (c >> 10));
                w[k++] = (uint16_t)(0xDC00 | (c & 0x3FF));
            } else {
                w[k++] = (uint16_t)c;
            }
        }
        w[k] = 0;
        wargv[i] = w;
    }
    wargv[crt_argc] = NULL;
    crt_wargc = crt_argc;
    crt_wargv = wargv;
    return 0;
}

int __cdecl _initialize_wide_environment(void) {
    if (crt_wenviron) return 0;
    const uint16_t* block = current_teb()->ProcessEnvironmentBlock->ProcessParameters->Environment;
    size_t count = 0;
    for (const uint16_t* p = block; p && *p; count++) {
        while (*p) p++;
        p++;
    }

    uint16_t** env = malloc((count + 1) * sizeof(uint16_t*));
    if (!env) return ENOMEM;
    size_t i = 0;
    for (const uint16_t* p = block; p && *p; i++) {
        const uint16_t* start = p;
        while (*p) p++;
        env[i] = wide_dup(start, (size_t)(p - start));
        p++;
    }
    env[i] = NULL;
    crt_wenviron = env;
    return 0;
}

int* __cdecl __p___wargc(void) {
    if (!crt_wargv) _configure_wide_argv(1);
    return &crt_wargc;
}

uint16_t*** __cdecl __p___wargv(void) {
    if (!crt_wargv) _configure_wide_argv(1);
    return &crt_wargv;
}

uint16_t*** __cdecl __p__wenviron(void) {
    if (!crt_wenviron) _initialize_wide_environment();
    return &crt_wenviron;
}

/* wWinMain に渡すコマンドライン: プログラム名を除いた残り */
uint16_t* __cdecl _get_wide_winmain_command_line(void) {
    uint16_t* p = current_teb()->ProcessEnvironmentBlock->ProcessParameters->CommandLine.Buffer;
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) {}
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static uint16_t* crt_wcmdln;
static char* crt_acmdln;

uint16_t** __cdecl __p__wcmdln(void) {
    if (!crt_wcmdln) crt_wcmdln = current_teb()->ProcessEnvironmentBlock->ProcessParameters->CommandLine.Buffer;
    return &crt_wcmdln;
}

char** __cdecl __p__acmdln(void) {
    if (!crt_acmdln) {
        ntmac_unicode_string* cmd = &current_teb()->ProcessEnvironmentBlock->ProcessParameters->CommandLine;
        crt_acmdln = to_utf8(cmd->Buffer, cmd->Length / sizeof(uint16_t));
    }
    return &crt_acmdln;
}
