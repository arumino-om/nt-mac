#pragma once
/*
 * NTMACHOST.DLL のインターフェース．PE 側 (ntdll) からホストへ入る唯一の入口．
 * ホスト (clang) と PE 側 (mingw) の両方から使うため C として書き，固定幅の型のみを使う．
 *
 *   ntmac_ntstatus ntmachost_call(uint32_t code, void* args);   // ms_abi
 */
#include <stdint.h>
#include "shared/ntstatus.h"
#include "shared/teb.h"

#define NTMACHOST_DLL_NAME   "ntmachost.dll"
#define NTMACHOST_ENTRY_NAME "ntmachost_call"

enum ntmachost_code {
    NTMACHOST_WRITE_FILE = 1,               /* ntmachost_io_args */
    NTMACHOST_READ_FILE = 2,                /* ntmachost_io_args */
    NTMACHOST_EXIT_PROCESS = 3,             /* ntmachost_exit_args (戻らない) */
    NTMACHOST_ALLOCATE_VIRTUAL_MEMORY = 4,  /* ntmachost_vm_args */
    NTMACHOST_FREE_VIRTUAL_MEMORY = 5,      /* ntmachost_vm_args */
    NTMACHOST_PROTECT_VIRTUAL_MEMORY = 6,   /* ntmachost_vm_args */
    NTMACHOST_QUERY_VIRTUAL_MEMORY = 7,     /* ntmachost_vm_query_args */
    NTMACHOST_DELAY_EXECUTION = 8,          /* ntmachost_delay_args */
    NTMACHOST_YIELD_EXECUTION = 9,          /* 引数なし */
    NTMACHOST_QUERY_SYSTEM_TIME = 10,       /* ntmachost_time_args */
    NTMACHOST_QUERY_PERFORMANCE_COUNTER = 11, /* ntmachost_perf_args */
    NTMACHOST_QUERY_FILE_TYPE = 12,         /* ntmachost_handle_args */
    NTMACHOST_CLOSE_HANDLE = 13,            /* ntmachost_handle_args */
    NTMACHOST_WAIT_ON_ADDRESS = 14,         /* ntmachost_wait_args */
    NTMACHOST_WAKE_BY_ADDRESS = 15,         /* ntmachost_wake_args */
    NTMACHOST_GET_MODULE_HANDLE = 16,       /* ntmachost_module_args: Name (NULL なら exe) → Module */
    NTMACHOST_GET_PROC_ADDRESS = 17,        /* ntmachost_module_args: Module, ProcName/Ordinal → Address */
    NTMACHOST_GET_MODULE_FILE_NAME = 18,    /* ntmachost_module_args: Module, Buffer/BufferLength */
    NTMACHOST_LOAD_LIBRARY = 19,            /* ntmachost_module_args: Name → Module (DllMain まで呼ぶ) */
    NTMACHOST_PC_TO_MODULE = 20,            /* ntmachost_module_args: Address → Module */
    NTMACHOST_SHUTDOWN_PROCESS = 21,        /* 引数なし．全 DLL の DllMain を DLL_PROCESS_DETACH で呼ぶ */
};

/* ファイルの種類 (Windows の FILE_TYPE_* と同じ値) */
#define NTMAC_FILE_TYPE_UNKNOWN 0
#define NTMAC_FILE_TYPE_DISK    1
#define NTMAC_FILE_TYPE_CHAR    2
#define NTMAC_FILE_TYPE_PIPE    3

/* 仮想メモリの状態・操作 (Windows の MEM_* と同じ値) */
#define NTMAC_MEM_COMMIT   0x00001000
#define NTMAC_MEM_RESERVE  0x00002000
#define NTMAC_MEM_DECOMMIT 0x00004000
#define NTMAC_MEM_RELEASE  0x00008000
#define NTMAC_MEM_FREE     0x00010000
#define NTMAC_MEM_PRIVATE  0x00020000

/* ページ保護 (Windows の PAGE_* と同じ値) */
#define NTMAC_PAGE_NOACCESS          0x01
#define NTMAC_PAGE_READONLY          0x02
#define NTMAC_PAGE_READWRITE         0x04
#define NTMAC_PAGE_WRITECOPY         0x08
#define NTMAC_PAGE_EXECUTE           0x10
#define NTMAC_PAGE_EXECUTE_READ      0x20
#define NTMAC_PAGE_EXECUTE_READWRITE 0x40
#define NTMAC_PAGE_EXECUTE_WRITECOPY 0x80

typedef struct ntmachost_io_args {
    uint64_t Handle;
    void* Buffer;
    uint32_t Length;
    uint32_t Transferred;       /* out */
} ntmachost_io_args;

typedef struct ntmachost_exit_args {
    uint32_t ExitCode;
} ntmachost_exit_args;

/* ALLOCATE: BaseAddress (0なら任意) と RegionSize を受け取り，確保した範囲を返す．Type は MEM_COMMIT / MEM_RESERVE
 * FREE:     Type は MEM_RELEASE (BaseAddress は確保時の先頭，RegionSize は 0) か MEM_DECOMMIT
 * PROTECT:  Protect に新しい保護を渡し，OldProtect に以前の保護を返す */
typedef struct ntmachost_vm_args {
    uint64_t BaseAddress;
    uint64_t RegionSize;
    uint32_t Type;
    uint32_t Protect;
    uint32_t OldProtect;
    uint32_t _pad;
} ntmachost_vm_args;

/* MEMORY_BASIC_INFORMATION 相当 */
typedef struct ntmachost_vm_query_args {
    uint64_t Address;           /* in */
    uint64_t BaseAddress;
    uint64_t AllocationBase;
    uint64_t RegionSize;
    uint32_t AllocationProtect;
    uint32_t State;
    uint32_t Protect;
    uint32_t Type;
} ntmachost_vm_query_args;

typedef struct ntmachost_delay_args {
    int64_t Interval;           /* 100ns 単位．負なら相対時間，正なら 1601年からの絶対時刻 (Windows と同じ) */
} ntmachost_delay_args;

typedef struct ntmachost_time_args {
    int64_t SystemTime;         /* 1601年からの 100ns 単位 (UTC) */
    int64_t TimeZoneBias;       /* UTC - 現地時刻 (100ns 単位．Windows の Bias と同じ符号) */
} ntmachost_time_args;

typedef struct ntmachost_perf_args {
    int64_t Counter;
    int64_t Frequency;
} ntmachost_perf_args;

typedef struct ntmachost_handle_args {
    uint64_t Handle;
    uint32_t Result;            /* QUERY_FILE_TYPE: NTMAC_FILE_TYPE_* */
    uint32_t _pad;
} ntmachost_handle_args;

/* *Address が CompareValue と等しい間，起こされるかタイムアウトするまで待つ (WaitOnAddress と同じ) */
typedef struct ntmachost_wait_args {
    const volatile void* Address;
    uint64_t CompareValue;
    uint32_t Size;              /* 1, 2, 4, 8 */
    uint32_t _pad;
    int64_t Timeout;            /* 100ns 単位の相対時間．負なら無期限 */
} ntmachost_wait_args;

typedef struct ntmachost_wake_args {
    const volatile void* Address;
    uint32_t WakeAll;
    uint32_t _pad;
} ntmachost_wake_args;

typedef struct ntmachost_module_args {
    const uint16_t* Name;       /* UTF-16 の DLL 名またはパス (終端付き) */
    uint64_t Module;            /* モジュールの先頭アドレス */
    const char* ProcName;       /* NULL なら Ordinal を使う */
    uint32_t Ordinal;
    uint32_t BufferLength;      /* in: Buffer の文字数，out: 書き込んだ文字数 (終端を含まない) */
    uint16_t* Buffer;
    void* Address;
} ntmachost_module_args;

NTMAC_STATIC_ASSERT(sizeof(ntmachost_io_args) == 24);
NTMAC_STATIC_ASSERT(sizeof(ntmachost_wait_args) == 32);
NTMAC_STATIC_ASSERT(sizeof(ntmachost_vm_args) == 32);
NTMAC_STATIC_ASSERT(sizeof(ntmachost_vm_query_args) == 48);

#ifdef _WIN32
/* PE 側からの宣言．import ライブラリは shared/ntmachost.def から生成する */
__declspec(dllimport) ntmac_ntstatus ntmachost_call(uint32_t code, void* args);
#endif
