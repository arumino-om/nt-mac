#pragma once
#include "host/win32k/state.h"

// Cocoa (AppKit) への反映．すべて g_lock を持った状態で呼んでよい (メインスレッドで非同期に実行される)．
// NSWindow はメインスレッドだけが持つ HWND → NSWindow の表で管理する．

namespace ntmac::win32k::cocoa {

/**
 * GUI を初期化し，画面の大きさを State に設定します．g_lock を持たずに呼ぶこと (メインスレッドの完了を待つため)．
 */
void prepare();

void show_window(const Window& w);
void hide_window(uint64_t hwnd);
void destroy_window(uint64_t hwnd);
void set_title(const Window& w);
void present(const Window& w);      // 描画面の内容を画面に反映する
void update_menu_bar(uint64_t hwnd); // hwnd がキーウィンドウならメニューバーを作り直す

}
