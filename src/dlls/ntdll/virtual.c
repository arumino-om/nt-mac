#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

static BOOL is_current_process(HANDLE process) {
    return process == NULL || process == NtCurrentProcess();
}

NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits,
                                       PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect) {
    (void)ZeroBits;
    if (!is_current_process(ProcessHandle)) return NTMAC_STATUS_NOT_IMPLEMENTED;

    ntmachost_vm_args args = {
        .BaseAddress = (uint64_t)*BaseAddress,
        .RegionSize = *RegionSize,
        .Type = AllocationType,
        .Protect = Protect,
    };
    NTSTATUS status = ntmachost_call(NTMACHOST_ALLOCATE_VIRTUAL_MEMORY, &args);
    if (NT_SUCCESS(status)) {
        *BaseAddress = (PVOID)args.BaseAddress;
        *RegionSize = args.RegionSize;
    }
    return status;
}

NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, PSIZE_T RegionSize, ULONG FreeType) {
    if (!is_current_process(ProcessHandle)) return NTMAC_STATUS_NOT_IMPLEMENTED;

    ntmachost_vm_args args = {
        .BaseAddress = (uint64_t)*BaseAddress,
        .RegionSize = *RegionSize,
        .Type = FreeType,
    };
    NTSTATUS status = ntmachost_call(NTMACHOST_FREE_VIRTUAL_MEMORY, &args);
    if (NT_SUCCESS(status)) {
        *BaseAddress = (PVOID)args.BaseAddress;
        *RegionSize = args.RegionSize;
    }
    return status;
}

NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, PSIZE_T RegionSize,
                                      ULONG NewProtect, PULONG OldProtect) {
    if (!is_current_process(ProcessHandle)) return NTMAC_STATUS_NOT_IMPLEMENTED;

    ntmachost_vm_args args = {
        .BaseAddress = (uint64_t)*BaseAddress,
        .RegionSize = *RegionSize,
        .Protect = NewProtect,
    };
    NTSTATUS status = ntmachost_call(NTMACHOST_PROTECT_VIRTUAL_MEMORY, &args);
    if (NT_SUCCESS(status)) {
        *BaseAddress = (PVOID)args.BaseAddress;
        *RegionSize = args.RegionSize;
        *OldProtect = args.OldProtect;
    }
    return status;
}

NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, MEMORY_INFORMATION_CLASS InformationClass,
                                    PVOID Information, SIZE_T Length, PSIZE_T ResultLength) {
    if (!is_current_process(ProcessHandle)) return NTMAC_STATUS_NOT_IMPLEMENTED;
    if (InformationClass != MemoryBasicInformation) return NTMAC_STATUS_NOT_IMPLEMENTED;
    if (Length < sizeof(MEMORY_BASIC_INFORMATION)) return NTMAC_STATUS_INFO_LENGTH_MISMATCH;

    ntmachost_vm_query_args args = {.Address = (uint64_t)BaseAddress};
    NTSTATUS status = ntmachost_call(NTMACHOST_QUERY_VIRTUAL_MEMORY, &args);
    if (!NT_SUCCESS(status)) return status;

    MEMORY_BASIC_INFORMATION* info = Information;
    info->BaseAddress = (PVOID)args.BaseAddress;
    info->AllocationBase = (PVOID)args.AllocationBase;
    info->AllocationProtect = args.AllocationProtect;
    info->PartitionId = 0;
    info->RegionSize = args.RegionSize;
    info->State = args.State;
    info->Protect = args.Protect;
    info->Type = args.Type;
    if (ResultLength) *ResultLength = sizeof(MEMORY_BASIC_INFORMATION);
    return status;
}
