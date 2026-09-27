#include "ucrtbase_private.h"

typedef void (__cdecl* crt_vfunc)(void);
typedef int (__cdecl* crt_ifunc)(void);
typedef void (__cdecl* crt_signal_handler)(int);

/* _onexit_table_t */
typedef struct onexit_table {
    crt_vfunc* first;
    crt_vfunc* last;
    crt_vfunc* end;
} onexit_table;

static onexit_table atexit_table;
static int crt_errno;
static int crt_fmode;
static int crt_commode;
static int new_mode;
static void* invalid_parameter_handler;

/* ---- 初期化関数の呼び出し ---- */

void __cdecl _initterm(crt_vfunc* begin, crt_vfunc* end) {
    for (; begin < end; begin++) {
        if (*begin) (*begin)();
    }
}

int __cdecl _initterm_e(crt_ifunc* begin, crt_ifunc* end) {
    for (; begin < end; begin++) {
        if (*begin) {
            int ret = (*begin)();
            if (ret) return ret;
        }
    }
    return 0;
}

/* ---- 終了時に呼ぶ関数 ---- */

int __cdecl _initialize_onexit_table(onexit_table* table) {
    if (!table) return -1;
    table->first = table->last = table->end = NULL;
    return 0;
}

int __cdecl _register_onexit_function(onexit_table* table, crt_vfunc func) {
    if (!table) return -1;
    if (table->last == table->end) {
        size_t count = (size_t)(table->end - table->first);
        size_t grown = count ? count * 2 : 32;
        crt_vfunc* p = realloc(table->first, grown * sizeof(crt_vfunc));
        if (!p) return -1;
        table->first = p;
        table->last = p + count;
        table->end = p + grown;
    }
    *table->last++ = func;
    return 0;
}

int __cdecl _execute_onexit_table(onexit_table* table) {
    if (!table) return -1;
    /* 登録と逆順に呼ぶ．呼び出し中に登録されたものも呼ばれるよう，毎回末尾から取り出す */
    while (table->last > table->first) {
        crt_vfunc func = *--table->last;
        if (func) func();
    }
    free(table->first);
    table->first = table->last = table->end = NULL;
    return 0;
}

int __cdecl _crt_atexit(crt_vfunc func) {
    return _register_onexit_function(&atexit_table, func);
}

int __cdecl atexit(crt_vfunc func) {
    return _crt_atexit(func);
}

void run_atexit_handlers(void) {
    _execute_onexit_table(&atexit_table);
}

/* ---- プロセスの終了 ---- */

void __cdecl _cexit(void) {
    run_atexit_handlers();
    stdio_flush_all();
}

void __cdecl _c_exit(void) {
}

__attribute__((noreturn)) void __cdecl exit(int code) {
    _cexit();
    ExitProcess((unsigned)code);
}

__attribute__((noreturn)) void __cdecl _exit(int code) {
    ExitProcess((unsigned)code);
}

__attribute__((noreturn)) void __cdecl abort(void) {
    /* SIGABRT のハンドラは呼ばず，Windows と同じ終了コード 3 で終わる */
    ExitProcess(3);
}

/* ---- シグナル (登録だけ受け付ける) ---- */

#define SIGINT   2
#define SIGILL   4
#define SIGFPE   8
#define SIGSEGV  11
#define SIGTERM  15
#define SIGBREAK 21
#define SIGABRT  22
#define SIG_ERR  ((crt_signal_handler)-1)

static crt_signal_handler signal_handlers[SIGABRT + 1];

crt_signal_handler __cdecl signal(int sig, crt_signal_handler handler) {
    switch (sig) {
    case SIGINT: case SIGILL: case SIGFPE: case SIGSEGV: case SIGTERM: case SIGBREAK: case SIGABRT: {
        crt_signal_handler old = signal_handlers[sig];
        signal_handlers[sig] = handler;
        return old;
    }
    default:
        crt_errno = EINVAL;
        return SIG_ERR;
    }
}

/* ---- 各種の設定 (値を保持するだけ) ---- */

int* __cdecl _errno(void) {
    return &crt_errno; /* TODO: スレッドごとに持つ */
}

int* __cdecl __p__fmode(void) {
    return &crt_fmode;
}

int* __cdecl __p__commode(void) {
    return &crt_commode;
}

void __cdecl _set_app_type(int type) {
    (void)type;
}

int __cdecl _set_new_mode(int mode) {
    int old = new_mode;
    new_mode = mode;
    return old;
}

void __cdecl __setusermatherr(void* handler) {
    (void)handler;
}

int __cdecl _configthreadlocale(int flag) {
    (void)flag;
    return 2; /* _DISABLE_PER_THREAD_LOCALE */
}

void* __cdecl _set_invalid_parameter_handler(void* handler) {
    return __atomic_exchange_n(&invalid_parameter_handler, handler, __ATOMIC_ACQ_REL);
}

/* SEH の __try/__except 用のハンドラ．例外のディスパッチは未実装なので，常に次のハンドラへ回す */
int __cdecl __C_specific_handler(void* record, void* frame, void* context, void* dispatch) {
    (void)record;
    (void)frame;
    (void)context;
    (void)dispatch;
    return 1; /* ExceptionContinueSearch */
}
