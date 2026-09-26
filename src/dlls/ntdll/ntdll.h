#pragma once
/*
 * ntdll.dll が公開する API の宣言 (PE 側から使う)．
 * mingw の winternl.h は ntdll の関数を dllimport 付きで宣言しているため使わず，必要な型はここで定義する．
 */
#include <windows.h>
#include "shared/ntstatus.h"
#include "shared/teb.h"

#ifdef NTMAC_BUILDING_NTDLL
#define NTMAC_NTDLL_API
#else
#define NTMAC_NTDLL_API __declspec(dllimport)
#endif

typedef LONG NTSTATUS;

#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef VOID (NTAPI* PIO_APC_ROUTINE)(PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG Reserved);

/* 現在のプロセスを表す疑似ハンドル */
#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)

/* ByteOffset の特別な値 */
#define FILE_WRITE_TO_END_OF_FILE      0xFFFFFFFF
#define FILE_USE_FILE_POINTER_POSITION 0xFFFFFFFE

static inline ntmac_teb* ntmac_current_teb(void) {
    return (ntmac_teb*)NtCurrentTeb();
}

NTMAC_NTDLL_API NTSTATUS NTAPI NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                           PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer,
                                           ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);
NTMAC_NTDLL_API NTSTATUS NTAPI NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
                                          PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer,
                                          ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);
NTMAC_NTDLL_API NTSTATUS NTAPI NtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus);

NTMAC_NTDLL_API ULONG NTAPI RtlNtStatusToDosError(NTSTATUS Status);
NTMAC_NTDLL_API DWORD NTAPI RtlGetLastWin32Error(void);
NTMAC_NTDLL_API void NTAPI RtlSetLastWin32Error(DWORD Error);
NTMAC_NTDLL_API ntmac_peb* NTAPI RtlGetCurrentPeb(void);
