#include <windows.h>

static int attached;              /* DllMain で 1 になる */
static int value = 40;
static int* volatile value_ptr = &value;   /* 絶対アドレスを持つので DIR64 リロケーションが必要 */

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) attached = 1;
    return TRUE;
}

/* 再配置と DllMain の両方が正しく行われていれば 42 を返す */
int Answer(void) {
    return *value_ptr + attached * 2;
}

/* 序数でのみエクスポートする (testdll.def を参照) */
int AnswerByOrdinal(void) {
    return 7;
}
