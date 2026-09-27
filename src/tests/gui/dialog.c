/*
 * ダイアログの確認．描画の後 (タイマー) にコントロールを操作し，Enter キーで閉じる．
 * 成功すると "dialog test ok" を出力し，終了コード 0 で終わる．
 */
#include <windows.h>
#include <stdio.h>

static int failures;
static int initialized;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static INT_PTR CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        initialized = 1;
        CHECK(lp == 42);
        WCHAR text[64];
        CHECK(GetDlgItemTextW(dlg, 101, text, 64) > 0 && wcscmp(text, L"テスト, バージョン 1.0") == 0);
        RECT rc;
        GetClientRect(dlg, &rc);
        CHECK(rc.right > 200 && rc.bottom > 80); /* ダイアログの単位から大きさが決まっている */
        SetTimer(dlg, 1, 50, NULL);
        return TRUE;
    }
    case WM_TIMER:
        KillTimer(dlg, 1);
        CHECK(GetFocus() == GetDlgItem(dlg, 103)); /* 最初のタブ位置 (チェックボックス) */
        CHECK(IsDlgButtonChecked(dlg, 103) == BST_UNCHECKED);
        SendDlgItemMessageW(dlg, 103, BM_CLICK, 0, 0);
        CHECK(IsDlgButtonChecked(dlg, 103) == BST_CHECKED);
        /* Enter キーは既定のボタン (OK) を押す */
        PostMessageW(dlg, WM_KEYDOWN, VK_RETURN, 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == 103) return TRUE;
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, LOWORD(wp));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev;
    (void)cmd;
    (void)show;
    INT_PTR result = DialogBoxParamW(inst, MAKEINTRESOURCEW(100), NULL, DlgProc, 42);
    CHECK(initialized);
    CHECK(result == IDOK);
    if (failures) {
        printf("%d failure(s), result=%d\n", failures, (int)result);
        return 1;
    }
    printf("dialog test ok\n");
    return 0;
}
