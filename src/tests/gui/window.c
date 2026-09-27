/*
 * ウィンドウの基本動作の確認．最初の描画の後で自分を閉じ，メッセージの順序を検査して終わる．
 * 成功すると "gui test ok" を出力し，終了コード 0 で終わる．
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char trace[512];
static int paints;

static void record(const char* s) {
    strcat(trace, s);
    strcat(trace, " ");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCREATE: record("NCCREATE"); break;
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        record(cs->lpCreateParams == (void*)0x1234 ? "CREATE" : "CREATE(bad-param)");
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 77);
        break;
    }
    case WM_SIZE: record("SIZE"); break;
    case WM_SHOWWINDOW: record("SHOWWINDOW"); break;
    case WM_ERASEBKGND: record("ERASEBKGND"); break;
    case WM_PAINT: {
        record("PAINT");
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        HBRUSH red = CreateSolidBrush(RGB(220, 40, 40));
        HBRUSH blue = CreateSolidBrush(RGB(40, 80, 220));
        RECT r1 = {20, 20, 220, 120}, r2 = {120, 80, 320, 200};
        FillRect(dc, &r1, red);
        FillRect(dc, &r2, blue);
        DeleteObject(red);
        DeleteObject(blue);
        EndPaint(hwnd, &ps);
        if (++paints == 1) PostMessageW(hwnd, WM_USER + 1, 5, 6);
        return 0;
    }
    case WM_USER + 1:
        record(wp == 5 && lp == 6 ? "USER" : "USER(bad-param)");
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE: record("CLOSE"); break;
    case WM_DESTROY:
        record(GetWindowLongPtrW(hwnd, GWLP_USERDATA) == 77 ? "DESTROY" : "DESTROY(bad-userdata)");
        PostQuitMessage(3);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev;
    (void)cmd;
    WNDCLASSEXW wc = {
        .cbSize = sizeof(wc),
        .lpfnWndProc = WndProc,
        .hInstance = inst,
        .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
        .lpszClassName = L"NtmacTestWindow",
    };
    if (!RegisterClassExW(&wc)) return 10;

    HWND hwnd = CreateWindowExW(0, L"NtmacTestWindow", L"nt-mac テスト", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 400, 300,
                                NULL, NULL, inst, (void*)0x1234);
    if (!hwnd) return 11;

    RECT rc;
    GetClientRect(hwnd, &rc);
    if (rc.right != 400 || rc.bottom != 300 - GetSystemMetrics(SM_CYCAPTION)) return 12;

    WCHAR title[64];
    if (GetWindowTextW(hwnd, title, 64) != (int)wcslen(L"nt-mac テスト") || wcscmp(title, L"nt-mac テスト") != 0) {
        return 13;
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    const char* expected =
        "NCCREATE CREATE SIZE SHOWWINDOW PAINT ERASEBKGND USER CLOSE SHOWWINDOW DESTROY ";
    if (strcmp(trace, expected) != 0 || msg.wParam != 3 || IsWindow(hwnd)) {
        printf("unexpected: %s(quit=%d)\n", trace, (int)msg.wParam);
        return 1;
    }
    printf("gui test ok\n");
    return 0;
}
