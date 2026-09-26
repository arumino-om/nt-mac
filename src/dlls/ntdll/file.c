#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

/* Event / APC / ファイル位置の指定は未対応 */
static NTSTATUS check_sync_io(HANDLE event, PIO_APC_ROUTINE apc, PLARGE_INTEGER offset) {
    if (event || apc) return NTMAC_STATUS_NOT_IMPLEMENTED;
    if (offset && offset->LowPart != FILE_USE_FILE_POINTER_POSITION &&
        !(offset->LowPart == FILE_WRITE_TO_END_OF_FILE && offset->HighPart == -1)) {
        return NTMAC_STATUS_NOT_IMPLEMENTED;
    }
    return NTMAC_STATUS_SUCCESS;
}

static NTSTATUS do_io(uint32_t code, HANDLE file, HANDLE event, PIO_APC_ROUTINE apc,
                      PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, PLARGE_INTEGER offset) {
    NTSTATUS status = check_sync_io(event, apc, offset);
    if (!NT_SUCCESS(status)) return status;

    ntmachost_io_args args = {
        .Handle = (uint64_t)(ULONG_PTR)file,
        .Buffer = buffer,
        .Length = length,
    };
    status = ntmachost_call(code, &args);

    iosb->Status = status;
    iosb->Information = args.Transferred;
    return status;
}

NTSTATUS NTAPI NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                           PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length,
                           PLARGE_INTEGER ByteOffset, PULONG Key) {
    (void)ApcContext;
    (void)Key;
    return do_io(NTMACHOST_WRITE_FILE, FileHandle, Event, ApcRoutine, IoStatusBlock, Buffer, Length, ByteOffset);
}

NTSTATUS NTAPI NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
                          PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length,
                          PLARGE_INTEGER ByteOffset, PULONG Key) {
    (void)ApcContext;
    (void)Key;
    NTSTATUS status = do_io(NTMACHOST_READ_FILE, FileHandle, Event, ApcRoutine, IoStatusBlock, Buffer, Length, ByteOffset);

    /* 0バイト読み込みはファイル終端 */
    if (NT_SUCCESS(status) && Length != 0 && IoStatusBlock->Information == 0) {
        status = NTMAC_STATUS_END_OF_FILE;
        IoStatusBlock->Status = status;
    }
    return status;
}
