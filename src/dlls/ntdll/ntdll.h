#pragma once
/*
 * ntdll.dll が公開する API の宣言 (PE 側から使う)．
 * mingw の winternl.h は ntdll の関数を dllimport 付きで宣言しているため使わず，必要な型はここで定義する．
 */
#ifdef NTMAC_BUILDING_NTDLL
#define _NTSYSTEM_  /* winnt.h の ntdll の関数の宣言から dllimport を外す */
#endif
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

typedef ntmac_unicode_string UNICODE_STRING, *PUNICODE_STRING;

typedef struct _ANSI_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} ANSI_STRING, *PANSI_STRING;

typedef struct _TIME_FIELDS {
    SHORT Year;
    SHORT Month;
    SHORT Day;
    SHORT Hour;
    SHORT Minute;
    SHORT Second;
    SHORT Milliseconds;
    SHORT Weekday;      /* 0 = 日曜 */
} TIME_FIELDS, *PTIME_FIELDS;

typedef enum _FS_INFORMATION_CLASS {
    FileFsDeviceInformation = 4,
} FS_INFORMATION_CLASS;

typedef struct _FILE_FS_DEVICE_INFORMATION {
    ULONG DeviceType;
    ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION;

#define FILE_DEVICE_DISK        0x00000007
#define FILE_DEVICE_NAMED_PIPE  0x00000011
#define FILE_DEVICE_CONSOLE     0x00000050
#define FILE_DEVICE_UNKNOWN     0x00000022

typedef VOID (NTAPI* PIO_APC_ROUTINE)(PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG Reserved);

/* 現在のプロセスを表す疑似ハンドル */
#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)

/* ByteOffset の特別な値 */
#define FILE_WRITE_TO_END_OF_FILE      0xFFFFFFFF
#define FILE_USE_FILE_POINTER_POSITION 0xFFFFFFFE

typedef enum _MEMORY_INFORMATION_CLASS {
    MemoryBasicInformation = 0,
} MEMORY_INFORMATION_CLASS;

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
NTMAC_NTDLL_API VOID NTAPI LdrShutdownProcess(void);
NTMAC_NTDLL_API __attribute__((noreturn)) VOID NTAPI RtlExitUserProcess(NTSTATUS ExitStatus);
NTMAC_NTDLL_API NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);
NTMAC_NTDLL_API NTSTATUS NTAPI NtYieldExecution(void);

NTMAC_NTDLL_API NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits,
                                                       PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect);
NTMAC_NTDLL_API NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, PSIZE_T RegionSize,
                                                   ULONG FreeType);
NTMAC_NTDLL_API NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, PSIZE_T RegionSize,
                                                      ULONG NewProtect, PULONG OldProtect);
NTMAC_NTDLL_API NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress,
                                                    MEMORY_INFORMATION_CLASS InformationClass, PVOID Information,
                                                    SIZE_T Length, PSIZE_T ResultLength);

NTMAC_NTDLL_API PVOID NTAPI RtlCreateHeap(ULONG Flags, PVOID HeapBase, SIZE_T ReserveSize, SIZE_T CommitSize,
                                          PVOID Lock, PVOID Parameters);
NTMAC_NTDLL_API PVOID NTAPI RtlAllocateHeap(PVOID HeapHandle, ULONG Flags, SIZE_T Size);
NTMAC_NTDLL_API BOOLEAN NTAPI RtlFreeHeap(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress);
NTMAC_NTDLL_API PVOID NTAPI RtlReAllocateHeap(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress, SIZE_T Size);
NTMAC_NTDLL_API SIZE_T NTAPI RtlSizeHeap(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress);

NTMAC_NTDLL_API NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlInitializeCriticalSectionAndSpinCount(PRTL_CRITICAL_SECTION CriticalSection,
                                                                        ULONG SpinCount);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
NTMAC_NTDLL_API BOOLEAN NTAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);

NTMAC_NTDLL_API NTSTATUS NTAPI NtClose(HANDLE Handle);
NTMAC_NTDLL_API NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                            PVOID Information, ULONG Length,
                                                            FS_INFORMATION_CLASS InformationClass);

/* 時刻 */
NTMAC_NTDLL_API NTSTATUS NTAPI NtQuerySystemTime(PLARGE_INTEGER SystemTime);
NTMAC_NTDLL_API NTSTATUS NTAPI NtQueryPerformanceCounter(PLARGE_INTEGER Counter, PLARGE_INTEGER Frequency);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlSystemTimeToLocalTime(const LARGE_INTEGER* SystemTime, PLARGE_INTEGER LocalTime);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlLocalTimeToSystemTime(const LARGE_INTEGER* LocalTime, PLARGE_INTEGER SystemTime);
NTMAC_NTDLL_API VOID NTAPI RtlTimeToTimeFields(const LARGE_INTEGER* Time, PTIME_FIELDS TimeFields);
NTMAC_NTDLL_API BOOLEAN NTAPI RtlTimeFieldsToTime(const TIME_FIELDS* TimeFields, PLARGE_INTEGER Time);

/* ローダ */
NTMAC_NTDLL_API NTSTATUS NTAPI LdrGetDllHandle(PWSTR DllPath, PULONG DllCharacteristics, const UNICODE_STRING* DllName,
                                               PVOID* DllHandle);
NTMAC_NTDLL_API NTSTATUS NTAPI LdrGetProcedureAddress(PVOID Module, const ANSI_STRING* Name, ULONG Ordinal,
                                                      PVOID* Address);
NTMAC_NTDLL_API NTSTATUS NTAPI LdrLoadDll(PWSTR DllPath, PULONG DllCharacteristics, const UNICODE_STRING* DllName,
                                          PVOID* DllHandle);
NTMAC_NTDLL_API NTSTATUS NTAPI LdrUnloadDll(PVOID DllHandle);
/* nt-mac 独自: モジュールの Windows のパス (Module が NULL なら exe)．Length は文字数 (終端を含む大きさ) */
NTMAC_NTDLL_API NTSTATUS NTAPI ntmac_LdrGetModuleFileName(PVOID Module, PWSTR Buffer, ULONG Length, PULONG Written);

/* リソース */
NTMAC_NTDLL_API NTSTATUS NTAPI LdrFindResource_U(PVOID BaseAddress, const ULONG_PTR* ResourceInfo, ULONG Level,
                                                 IMAGE_RESOURCE_DATA_ENTRY** ResourceDataEntry);
NTMAC_NTDLL_API NTSTATUS NTAPI LdrAccessResource(PVOID BaseAddress, const IMAGE_RESOURCE_DATA_ENTRY* ResourceDataEntry,
                                                 PVOID* Resource, PULONG Size);

/* 同期 */
NTMAC_NTDLL_API NTSTATUS NTAPI RtlWaitOnAddress(const volatile void* Address, PVOID CompareAddress, SIZE_T Size,
                                                PLARGE_INTEGER Timeout);
NTMAC_NTDLL_API VOID NTAPI RtlWakeAddressSingle(PVOID Address);
NTMAC_NTDLL_API VOID NTAPI RtlWakeAddressAll(PVOID Address);
NTMAC_NTDLL_API VOID NTAPI RtlInitializeSRWLock(PRTL_SRWLOCK Lock);
NTMAC_NTDLL_API VOID NTAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK Lock);
NTMAC_NTDLL_API VOID NTAPI RtlAcquireSRWLockShared(PRTL_SRWLOCK Lock);
NTMAC_NTDLL_API VOID NTAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK Lock);
NTMAC_NTDLL_API VOID NTAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK Lock);
NTMAC_NTDLL_API BOOLEAN NTAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK Lock);
NTMAC_NTDLL_API BOOLEAN NTAPI RtlTryAcquireSRWLockShared(PRTL_SRWLOCK Lock);
NTMAC_NTDLL_API VOID NTAPI RtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE Cond);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlSleepConditionVariableSRW(PRTL_CONDITION_VARIABLE Cond, PRTL_SRWLOCK Lock,
                                                            const LARGE_INTEGER* Timeout, ULONG Flags);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlSleepConditionVariableCS(PRTL_CONDITION_VARIABLE Cond, PRTL_CRITICAL_SECTION Cs,
                                                           const LARGE_INTEGER* Timeout);
NTMAC_NTDLL_API VOID NTAPI RtlWakeConditionVariable(PRTL_CONDITION_VARIABLE Cond);
NTMAC_NTDLL_API VOID NTAPI RtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE Cond);
NTMAC_NTDLL_API VOID NTAPI RtlRunOnceInitialize(PRTL_RUN_ONCE Once);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlRunOnceExecuteOnce(PRTL_RUN_ONCE Once, PRTL_RUN_ONCE_INIT_FN Func, PVOID Param,
                                                     PVOID* Context);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlInitializeCriticalSectionEx(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount,
                                                              ULONG Flags);

/* バージョン */
NTMAC_NTDLL_API NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW Info);
NTMAC_NTDLL_API NTSTATUS NTAPI RtlVerifyVersionInfo(PRTL_OSVERSIONINFOEXW Info, ULONG TypeMask, ULONGLONG ConditionMask);

/* その他 */
NTMAC_NTDLL_API PVOID NTAPI RtlEncodePointer(PVOID Ptr);
NTMAC_NTDLL_API PVOID NTAPI RtlDecodePointer(PVOID Ptr);
NTMAC_NTDLL_API BOOLEAN NTAPI RtlIsProcessorFeaturePresent(ULONG Feature);
void ntmac_init_pointer_cookie(void);  /* ntdll 内部 */

NTMAC_NTDLL_API ULONG NTAPI RtlNtStatusToDosError(NTSTATUS Status);
NTMAC_NTDLL_API DWORD NTAPI RtlGetLastWin32Error(void);
NTMAC_NTDLL_API void NTAPI RtlSetLastWin32Error(DWORD Error);
NTMAC_NTDLL_API ntmac_peb* NTAPI RtlGetCurrentPeb(void);
