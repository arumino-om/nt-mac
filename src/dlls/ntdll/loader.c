#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

/* ローダの本体はホスト側にあり，ここでは NTMACHOST に問い合わせる */

#define NAME_BUFFER_LENGTH 1024

/* UNICODE_STRING は終端が無いことがあるので，終端付きの領域に写す */
static NTSTATUS copy_name(const UNICODE_STRING* name, uint16_t* buf) {
    size_t len = name->Length / sizeof(uint16_t);
    if (len >= NAME_BUFFER_LENGTH) return NTMAC_STATUS_INVALID_PARAMETER;
    memcpy(buf, name->Buffer, len * sizeof(uint16_t));
    buf[len] = 0;
    return NTMAC_STATUS_SUCCESS;
}

NTSTATUS NTAPI LdrGetDllHandle(PWSTR DllPath, PULONG DllCharacteristics, const UNICODE_STRING* DllName,
                               PVOID* DllHandle) {
    (void)DllPath;
    (void)DllCharacteristics;
    uint16_t buf[NAME_BUFFER_LENGTH];
    NTSTATUS status = copy_name(DllName, buf);
    if (!NT_SUCCESS(status)) return status;

    ntmachost_module_args args = {.Name = buf};
    status = ntmachost_call(NTMACHOST_GET_MODULE_HANDLE, &args);
    *DllHandle = NT_SUCCESS(status) ? (PVOID)args.Module : NULL;
    return status;
}

NTSTATUS NTAPI LdrGetProcedureAddress(PVOID Module, const ANSI_STRING* Name, ULONG Ordinal, PVOID* Address) {
    char buf[NAME_BUFFER_LENGTH];
    ntmachost_module_args args = {.Module = (uint64_t)Module, .Ordinal = Ordinal};
    if (Name) {
        if (Name->Length >= sizeof(buf)) return NTMAC_STATUS_INVALID_PARAMETER;
        memcpy(buf, Name->Buffer, Name->Length);
        buf[Name->Length] = 0;
        args.ProcName = buf;
    }

    NTSTATUS status = ntmachost_call(NTMACHOST_GET_PROC_ADDRESS, &args);
    *Address = NT_SUCCESS(status) ? args.Address : NULL;
    return status;
}

NTSTATUS NTAPI LdrLoadDll(PWSTR DllPath, PULONG DllCharacteristics, const UNICODE_STRING* DllName, PVOID* DllHandle) {
    (void)DllPath;
    (void)DllCharacteristics;
    uint16_t buf[NAME_BUFFER_LENGTH];
    NTSTATUS status = copy_name(DllName, buf);
    if (!NT_SUCCESS(status)) return status;

    ntmachost_module_args args = {.Name = buf};
    status = ntmachost_call(NTMACHOST_LOAD_LIBRARY, &args);
    *DllHandle = NT_SUCCESS(status) ? (PVOID)args.Module : NULL;
    return status;
}

NTSTATUS NTAPI LdrUnloadDll(PVOID DllHandle) {
    (void)DllHandle; /* DLL の解放は未実装 (読み込んだままにする) */
    return NTMAC_STATUS_SUCCESS;
}

NTSTATUS NTAPI ntmac_LdrGetModuleFileName(PVOID Module, PWSTR Buffer, ULONG Length, PULONG Written) {
    ntmachost_module_args args = {
        .Module = (uint64_t)Module,
        .Buffer = (uint16_t*)Buffer,
        .BufferLength = Length,
    };
    NTSTATUS status = ntmachost_call(NTMACHOST_GET_MODULE_FILE_NAME, &args);
    *Written = args.BufferLength;
    return status;
}

PVOID NTAPI RtlPcToFileHeader(PVOID PcValue, PVOID* BaseOfImage) {
    ntmachost_module_args args = {.Address = PcValue};
    NTSTATUS status = ntmachost_call(NTMACHOST_PC_TO_MODULE, &args);
    *BaseOfImage = NT_SUCCESS(status) ? (PVOID)args.Module : NULL;
    return *BaseOfImage;
}
