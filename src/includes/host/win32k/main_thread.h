#pragma once

// プロセスのメインスレッド (Cocoa / AppKit はメインスレッドでしか動かない)

namespace ntmac::win32k {

/**
 * メインスレッドで実行ループを回します．戻りません (プロセスは PE のスレッドが exit して終わる)．
 */
[[noreturn]] void run_main_thread();

/**
 * GUI (NSApplication) を使えるようにします．初回だけメインスレッドで AppKit を初期化します．
 * 環境変数 NTMAC_NO_DISPLAY が設定されていれば何もせず false を返します (ウィンドウはホストの中にだけ存在する)．
 */
bool ensure_gui();

/**
 * block をメインスレッドで非同期に実行します．
 */
void run_on_main_async(void (^block)(void));

/**
 * block をメインスレッドで実行し，終わるまで待ちます (メインスレッドから呼ばれた場合はそのまま実行する)．
 */
void run_on_main_sync(void (^block)(void));

}
