#include <windows.h>

__declspec(dllimport) int Answer(void);
__declspec(dllimport) int AnswerByOrdinal(void);
__declspec(dllimport) void TestExit(UINT code);   /* kernel32.ExitProcess へのフォワーダ */

extern IMAGE_DOS_HEADER __ImageBase;

static void check(int ok, UINT code) {
    if (!ok) TestExit(code);
}

void start(void) {
    /* gs:[0x30] が TEB 自身を指していること */
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    check(tib->Self == tib, 10);
    check((char*)&tib < (char*)tib->StackBase && (char*)&tib >= (char*)tib->StackLimit, 11);

    /* TEB->ProcessEnvironmentBlock (0x60) の PEB->ImageBaseAddress (0x10) が自分自身 */
    void* peb = *(void**)((char*)tib + 0x60);
    check(*(void**)((char*)peb + 0x10) == &__ImageBase, 12);

    check(Answer() == 42, 20);
    check(AnswerByOrdinal() == 7, 21);

    /* LastError は TEB (0x68) に保存される */
    SetLastError(1234);
    check(GetLastError() == 1234, 30);
    check(*(DWORD*)((char*)tib + 0x68) == 1234, 31);

    DWORD n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "dll test ok\n", 12, &n, NULL);
    TestExit(0);
}
