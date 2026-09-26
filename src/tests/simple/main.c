#include <windows.h>

void start(void) {
    DWORD n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Hello\n", 6, &n, NULL);
    ExitProcess(0);
}