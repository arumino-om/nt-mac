#include "ntdll/ntdll.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;

    /* Windows ではプロセスの初期化時にローダ (ntdll) がプロセスヒープを作る */
    if (reason == DLL_PROCESS_ATTACH) {
        ntmac_init_pointer_cookie();
        ntmac_peb* peb = RtlGetCurrentPeb();
        if (!peb->ProcessHeap) {
            peb->ProcessHeap = RtlCreateHeap(HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
            if (!peb->ProcessHeap) return FALSE;
        }
    }
    return TRUE;
}
