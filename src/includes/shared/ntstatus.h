#pragma once
/*
 * nt-mac で使う NTSTATUS の値．ホストと PE 側の両方から使う．
 * mingw の ntstatus.h と名前が衝突しないよう NTMAC_ を付けている．
 */
#include <stdint.h>

typedef int32_t ntmac_ntstatus;

#define NTMAC_STATUS_SUCCESS                ((ntmac_ntstatus)0x00000000)
#define NTMAC_STATUS_UNSUCCESSFUL           ((ntmac_ntstatus)0xC0000001)
#define NTMAC_STATUS_NOT_IMPLEMENTED        ((ntmac_ntstatus)0xC0000002)
#define NTMAC_STATUS_INVALID_HANDLE         ((ntmac_ntstatus)0xC0000008)
#define NTMAC_STATUS_INVALID_PARAMETER      ((ntmac_ntstatus)0xC000000D)
#define NTMAC_STATUS_END_OF_FILE            ((ntmac_ntstatus)0xC0000011)
#define NTMAC_STATUS_INVALID_SYSTEM_SERVICE ((ntmac_ntstatus)0xC000001C)
