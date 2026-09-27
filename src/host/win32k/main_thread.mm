#import <AppKit/AppKit.h>
#include <cstdlib>
#include <mutex>
#include "host/win32k/main_thread.h"

namespace ntmac::win32k {

namespace {
bool gui_started = false; // メインスレッドからのみ参照
}

void run_main_thread() {
    // 実行ループに何も無いと CFRunLoopRun がすぐ戻るので，常駐用のタイマーを置いておく
    CFRunLoopTimerRef keepalive = CFRunLoopTimerCreate(nullptr, CFAbsoluteTimeGetCurrent() + 1e10, 1e10, 0, 0,
                                                       [](CFRunLoopTimerRef, void*) {}, nullptr);
    CFRunLoopAddTimer(CFRunLoopGetMain(), keepalive, kCFRunLoopCommonModes);

    // GUI が要求されるまでは CFRunLoop だけを回す (コンソールアプリでは AppKit を起動しない)．
    // [NSApp run] をメインキューのブロックの中から呼ぶと，それ以降のブロックが実行されなくなるので，ここで呼ぶ
    while (!gui_started) CFRunLoopRun();
    [NSApp run];
    for (;;) CFRunLoopRun();
}

bool ensure_gui() {
    static const bool no_display = std::getenv("NTMAC_NO_DISPLAY") != nullptr;
    if (no_display) return false;

    static std::once_flag once;
    std::call_once(once, [] {
        run_on_main_sync(^{
            [NSApplication sharedApplication];
            // 最初のウィンドウを作る時点で通常のアプリになる (Dock にアイコンが出る)
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];
            // run_main_thread の CFRunLoopRun を抜けさせ，[NSApp run] に移らせる
            gui_started = true;
            CFRunLoopStop(CFRunLoopGetMain());
        });
    });
    return true;
}

void run_on_main_async(void (^block)(void)) {
    dispatch_async(dispatch_get_main_queue(), block);
}

void run_on_main_sync(void (^block)(void)) {
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
}

}
