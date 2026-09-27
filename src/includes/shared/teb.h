#pragma once
/*
 * TEB / PEB の定義．ホスト (clang, LP64) と PE 側 (mingw, LLP64) の両方から使うため，
 * C として書き，long のような環境依存の型は使わない．
 * 未実装のフィールドは _reserved で埋め，オフセットは Windows 10 x64 に合わせている．
 */

/* 0x1838 (Windows 10 の TEB サイズ) より後ろは nt-mac 独自のホスト用領域 */
#define NTMAC_TEB_SIZE     0x2000
#define NTMAC_TEB_HOST_TSD 0x1F00

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define NTMAC_STATIC_ASSERT(e) static_assert(e, #e)
#else
#define NTMAC_STATIC_ASSERT(e) _Static_assert(e, #e)
#endif

typedef struct ntmac_unicode_string {
    uint16_t Length;        /* バイト数 (終端を含まない) */
    uint16_t MaximumLength;
    uint32_t _pad;
    uint16_t* Buffer;       /* UTF-16 */
} ntmac_unicode_string;

typedef struct ntmac_list_entry {
    struct ntmac_list_entry* Flink;
    struct ntmac_list_entry* Blink;
} ntmac_list_entry;

/* RTL_USER_PROCESS_PARAMETERS (先頭部分のみ) */
typedef struct ntmac_process_parameters {
    uint32_t MaximumLength;
    uint32_t Length;
    uint32_t Flags;
    uint32_t DebugFlags;
    void* ConsoleHandle;
    uint32_t ConsoleFlags;
    uint32_t _pad0;
    void* StandardInput;
    void* StandardOutput;
    void* StandardError;
    ntmac_unicode_string CurrentDirectoryPath;
    void* CurrentDirectoryHandle;
    ntmac_unicode_string DllPath;
    ntmac_unicode_string ImagePathName;
    ntmac_unicode_string CommandLine;
    void* Environment;
} ntmac_process_parameters;

typedef struct ntmac_peb {
    uint8_t InheritedAddressSpace;
    uint8_t ReadImageFileExecOptions;
    uint8_t BeingDebugged;
    uint8_t BitField;
    uint32_t _pad0;
    void* Mutant;
    void* ImageBaseAddress;
    void* Ldr;                                    /* PEB_LDR_DATA (未実装) */
    ntmac_process_parameters* ProcessParameters;
    void* SubSystemData;
    void* ProcessHeap;
    uint8_t _reserved0[0xB8 - 0x38];
    uint32_t NumberOfProcessors;
    uint32_t NtGlobalFlag;
    uint8_t _reserved1[0x118 - 0xC0];
    uint32_t OSMajorVersion;
    uint32_t OSMinorVersion;
    uint16_t OSBuildNumber;
    uint16_t OSCSDVersion;
    uint32_t OSPlatformId;
    uint32_t ImageSubsystem;
    uint32_t ImageSubsystemMajorVersion;
    uint32_t ImageSubsystemMinorVersion;
    uint8_t _reserved2[0x7C8 - 0x134];
} ntmac_peb;

typedef struct ntmac_teb {
    /* NT_TIB */
    void* ExceptionList;
    void* StackBase;
    void* StackLimit;
    void* SubSystemTib;
    void* FiberData;
    void* ArbitraryUserPointer;
    struct ntmac_teb* Self;

    void* EnvironmentPointer;
    uint64_t UniqueProcess;                       /* CLIENT_ID */
    uint64_t UniqueThread;
    void* ActiveRpcHandle;
    void* ThreadLocalStoragePointer;
    ntmac_peb* ProcessEnvironmentBlock;
    uint32_t LastErrorValue;
    uint8_t _reserved0[0x1250 - 0x6C];
    uint32_t LastStatusValue;
    uint8_t _reserved1[0x1478 - 0x1254];
    void* DeallocationStack;
    void* TlsSlots[64];
    ntmac_list_entry TlsLinks;
    uint8_t _reserved2[0x1780 - 0x1690];
    void** TlsExpansionSlots;
    uint8_t _reserved3[0x17C8 - 0x1788];
    void* FlsData;                                /* kernel32 の Fls* が使う (nt-mac では void*[] を指す) */
    uint8_t _reserved4[NTMAC_TEB_HOST_TSD - 0x17D0];

    /* nt-mac 独自: PE→ホストの切り替え時に gs ベースへ設定する，このスレッドの macOS TSD */
    void* HostTsd;
    uint8_t _reserved5[NTMAC_TEB_SIZE - NTMAC_TEB_HOST_TSD - 8];
} ntmac_teb;

NTMAC_STATIC_ASSERT(sizeof(ntmac_unicode_string) == 0x10);
NTMAC_STATIC_ASSERT(offsetof(ntmac_process_parameters, StandardInput) == 0x20);
NTMAC_STATIC_ASSERT(offsetof(ntmac_process_parameters, ImagePathName) == 0x60);
NTMAC_STATIC_ASSERT(offsetof(ntmac_process_parameters, CommandLine) == 0x70);
NTMAC_STATIC_ASSERT(offsetof(ntmac_process_parameters, Environment) == 0x80);

NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, BeingDebugged) == 0x02);
NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, ImageBaseAddress) == 0x10);
NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, Ldr) == 0x18);
NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, ProcessParameters) == 0x20);
NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, ProcessHeap) == 0x30);
NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, NumberOfProcessors) == 0xB8);
NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, OSMajorVersion) == 0x118);
NTMAC_STATIC_ASSERT(offsetof(ntmac_peb, ImageSubsystem) == 0x128);
NTMAC_STATIC_ASSERT(sizeof(ntmac_peb) == 0x7C8);

NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, StackBase) == 0x08);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, StackLimit) == 0x10);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, Self) == 0x30);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, UniqueProcess) == 0x40);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, ThreadLocalStoragePointer) == 0x58);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, ProcessEnvironmentBlock) == 0x60);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, LastErrorValue) == 0x68);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, LastStatusValue) == 0x1250);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, DeallocationStack) == 0x1478);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, TlsSlots) == 0x1480);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, TlsExpansionSlots) == 0x1780);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, FlsData) == 0x17C8);
NTMAC_STATIC_ASSERT(offsetof(ntmac_teb, HostTsd) == NTMAC_TEB_HOST_TSD);
NTMAC_STATIC_ASSERT(sizeof(ntmac_teb) == NTMAC_TEB_SIZE);

#endif /* __ASSEMBLER__ */
