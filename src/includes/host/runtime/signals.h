#pragma once
#include "host/loader/module_manager.h"

namespace ntmac::runtime {

/**
 * SIGSEGV などの致命的なシグナルのハンドラを登録します．
 * PE コードの実行中 (gs が TEB を指している状態) に発生した場合でも，gs をホストに戻してから
 * 発生場所を表示し，既定の動作 (異常終了) に任せます．
 */
void install_crash_handler(const loader::ModuleManager* modules);

}
