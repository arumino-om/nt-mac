#pragma once
/*
 * nt-mac で使う NTSTATUS の値．ホストと PE 側の両方から使う．
 * mingw の ntstatus.h と名前が衝突しないよう NTMAC_ を付けている．
 */
#include <stdint.h>

typedef int32_t ntmac_ntstatus;

#define NTMAC_STATUS_SUCCESS                ((ntmac_ntstatus)0x00000000)
#define NTMAC_STATUS_TIMEOUT                ((ntmac_ntstatus)0x00000102)
#define NTMAC_STATUS_UNSUCCESSFUL           ((ntmac_ntstatus)0xC0000001)
#define NTMAC_STATUS_NOT_IMPLEMENTED        ((ntmac_ntstatus)0xC0000002)
#define NTMAC_STATUS_INFO_LENGTH_MISMATCH   ((ntmac_ntstatus)0xC0000004)
#define NTMAC_STATUS_INVALID_HANDLE         ((ntmac_ntstatus)0xC0000008)
#define NTMAC_STATUS_INVALID_PARAMETER      ((ntmac_ntstatus)0xC000000D)
#define NTMAC_STATUS_END_OF_FILE            ((ntmac_ntstatus)0xC0000011)
#define NTMAC_STATUS_BUFFER_TOO_SMALL       ((ntmac_ntstatus)0xC0000023)
#define NTMAC_STATUS_NO_MEMORY              ((ntmac_ntstatus)0xC0000017)
#define NTMAC_STATUS_CONFLICTING_ADDRESSES  ((ntmac_ntstatus)0xC0000018)
#define NTMAC_STATUS_INVALID_PAGE_PROTECTION ((ntmac_ntstatus)0xC0000045)
#define NTMAC_STATUS_REVISION_MISMATCH     ((ntmac_ntstatus)0xC0000059)
#define NTMAC_STATUS_PROCEDURE_NOT_FOUND    ((ntmac_ntstatus)0xC000007A)
#define NTMAC_STATUS_MEMORY_NOT_ALLOCATED   ((ntmac_ntstatus)0xC00000A0)
#define NTMAC_STATUS_RESOURCE_DATA_NOT_FOUND ((ntmac_ntstatus)0xC0000089)
#define NTMAC_STATUS_RESOURCE_TYPE_NOT_FOUND ((ntmac_ntstatus)0xC000008A)
#define NTMAC_STATUS_RESOURCE_NAME_NOT_FOUND ((ntmac_ntstatus)0xC000008B)
#define NTMAC_STATUS_RESOURCE_LANG_NOT_FOUND ((ntmac_ntstatus)0xC0000204)
#define NTMAC_STATUS_DLL_NOT_FOUND          ((ntmac_ntstatus)0xC0000135)
#define NTMAC_STATUS_DLL_INIT_FAILED        ((ntmac_ntstatus)0xC0000142)
#define NTMAC_STATUS_INVALID_SYSTEM_SERVICE ((ntmac_ntstatus)0xC000001C)
