#include "user32_private.h"

void register_dialog_classes(HINSTANCE user32);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) register_dialog_classes(instance); /* ダイアログと標準コントロールのクラス */
    return TRUE;
}
