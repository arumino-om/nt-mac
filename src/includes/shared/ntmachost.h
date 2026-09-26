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
    NTMACHOST_WRITE_FILE = 1,   /* ntmachost_io_args */
    NTMACHOST_READ_FILE = 2,    /* ntmachost_io_args */
    NTMACHOST_EXIT_PROCESS = 3, /* ntmachost_exit_args (戻らない) */
};

typedef struct ntmachost_io_args {
    uint64_t Handle;
    void* Buffer;
    uint32_t Length;
    uint32_t Transferred;       /* out */
} ntmachost_io_args;

typedef struct ntmachost_exit_args {
    uint32_t ExitCode;
} ntmachost_exit_args;

NTMAC_STATIC_ASSERT(sizeof(ntmachost_io_args) == 24);

#ifdef _WIN32
/* PE 側からの宣言．import ライブラリは shared/ntmachost.def から生成する */
__declspec(dllimport) ntmac_ntstatus ntmachost_call(uint32_t code, void* args);
#endif
