#include <cpuid.h>
#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

/* ---- ハンドル ---- */

NTSTATUS NTAPI NtClose(HANDLE Handle) {
    ntmachost_handle_args args = {.Handle = (uint64_t)Handle};
    return ntmachost_call(NTMACHOST_CLOSE_HANDLE, &args);
}

NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID Information,
                                            ULONG Length, FS_INFORMATION_CLASS InformationClass) {
    if (InformationClass != FileFsDeviceInformation) return NTMAC_STATUS_NOT_IMPLEMENTED;
    if (Length < sizeof(FILE_FS_DEVICE_INFORMATION)) return NTMAC_STATUS_INFO_LENGTH_MISMATCH;

    ntmachost_handle_args args = {.Handle = (uint64_t)FileHandle};
    NTSTATUS status = ntmachost_call(NTMACHOST_QUERY_FILE_TYPE, &args);
    if (NT_SUCCESS(status)) {
        FILE_FS_DEVICE_INFORMATION* info = Information;
        switch (args.Result) {
        case NTMAC_FILE_TYPE_DISK: info->DeviceType = FILE_DEVICE_DISK; break;
        case NTMAC_FILE_TYPE_CHAR: info->DeviceType = FILE_DEVICE_CONSOLE; break;
        case NTMAC_FILE_TYPE_PIPE: info->DeviceType = FILE_DEVICE_NAMED_PIPE; break;
        default:                   info->DeviceType = FILE_DEVICE_UNKNOWN; break;
        }
        info->Characteristics = 0;
    }
    IoStatusBlock->Status = status;
    IoStatusBlock->Information = sizeof(FILE_FS_DEVICE_INFORMATION);
    return status;
}

/* ---- ポインタの符号化 (EncodePointer) ---- */

static ULONG_PTR pointer_cookie;

void ntmac_init_pointer_cookie(void) {
    LARGE_INTEGER counter;
    NtQueryPerformanceCounter(&counter, NULL);
    pointer_cookie = (ULONG_PTR)counter.QuadPart * 0x9E3779B97F4A7C15ULL ^ (ULONG_PTR)&pointer_cookie;
}

static ULONG_PTR rotate_right(ULONG_PTR v, unsigned n) {
    n &= 63;
    return n ? (v >> n) | (v << (64 - n)) : v;
}

PVOID NTAPI RtlEncodePointer(PVOID Ptr) {
    return (PVOID)rotate_right((ULONG_PTR)Ptr ^ pointer_cookie, (unsigned)(pointer_cookie & 63));
}

PVOID NTAPI RtlDecodePointer(PVOID Ptr) {
    unsigned n = (unsigned)(pointer_cookie & 63);
    return (PVOID)(rotate_right((ULONG_PTR)Ptr, 64 - n) ^ pointer_cookie);
}

/* ---- CPU の機能 ---- */

BOOLEAN NTAPI RtlIsProcessorFeaturePresent(ULONG Feature) {
    unsigned eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    unsigned ext_ebx = 0;
    if (__get_cpuid_max(0, NULL) >= 7) {
        unsigned a, c, d;
        __cpuid_count(7, 0, a, ext_ebx, c, d);
    }

    switch (Feature) {
    case PF_FLOATING_POINT_PRECISION_ERRATA: return FALSE;
    case PF_FLOATING_POINT_EMULATED:         return FALSE;
    case PF_COMPARE_EXCHANGE_DOUBLE:         return TRUE;
    case PF_MMX_INSTRUCTIONS_AVAILABLE:      return (edx >> 23) & 1;
    case PF_XMMI_INSTRUCTIONS_AVAILABLE:     return (edx >> 25) & 1;
    case PF_RDTSC_INSTRUCTION_AVAILABLE:     return (edx >> 4) & 1;
    case PF_PAE_ENABLED:                     return TRUE;
    case PF_XMMI64_INSTRUCTIONS_AVAILABLE:   return (edx >> 26) & 1;
    case PF_NX_ENABLED:                      return TRUE;
    case PF_SSE3_INSTRUCTIONS_AVAILABLE:     return ecx & 1;
    case PF_COMPARE_EXCHANGE128:             return (ecx >> 13) & 1;
    case PF_XSAVE_ENABLED:                   return (ecx >> 27) & 1;
    case PF_FASTFAIL_AVAILABLE:              return TRUE;
    case PF_SSSE3_INSTRUCTIONS_AVAILABLE:    return (ecx >> 9) & 1;
    case PF_SSE4_1_INSTRUCTIONS_AVAILABLE:   return (ecx >> 19) & 1;
    case PF_SSE4_2_INSTRUCTIONS_AVAILABLE:   return (ecx >> 20) & 1;
    case PF_AVX_INSTRUCTIONS_AVAILABLE:      return (ecx >> 28) & 1;
    case PF_AVX2_INSTRUCTIONS_AVAILABLE:     return (ext_ebx >> 5) & 1;
    default:                                 return FALSE;
    }
}
