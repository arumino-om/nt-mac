#include "ucrtbase_private.h"

#define DLL_PROCESS_ATTACH 1

BOOL WINAPI DllMain(HANDLE instance, DWORD reason, void* reserved) {
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        heap_init();
        stdio_init();
    }
    return 1;
}
