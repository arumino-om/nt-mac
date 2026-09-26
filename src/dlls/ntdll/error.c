#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

ULONG NTAPI RtlNtStatusToDosError(NTSTATUS Status) {
    ntmac_current_teb()->LastStatusValue = (uint32_t)Status;

    switch (Status) {
    case NTMAC_STATUS_SUCCESS:           return ERROR_SUCCESS;
    case NTMAC_STATUS_UNSUCCESSFUL:      return ERROR_GEN_FAILURE;
    case NTMAC_STATUS_NOT_IMPLEMENTED:   return ERROR_INVALID_FUNCTION;
    case NTMAC_STATUS_INVALID_HANDLE:    return ERROR_INVALID_HANDLE;
    case NTMAC_STATUS_INVALID_PARAMETER: return ERROR_INVALID_PARAMETER;
    case NTMAC_STATUS_END_OF_FILE:       return ERROR_HANDLE_EOF;
    default:                             return ERROR_MR_MID_NOT_FOUND; /* Windows でも対応が無い場合はこの値 */
    }
}

DWORD NTAPI RtlGetLastWin32Error(void) {
    return ntmac_current_teb()->LastErrorValue;
}

void NTAPI RtlSetLastWin32Error(DWORD Error) {
    ntmac_current_teb()->LastErrorValue = Error;
}
