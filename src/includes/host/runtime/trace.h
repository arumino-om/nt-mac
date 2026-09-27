#pragma once
#include <string>
#include <string_view>

namespace ntmac::loader {
class ModuleManager;
}

// デバッグ用のトレースと，未実装の関数のスタブ．
//
// 環境変数:
//   NTMAC_TRACE=<項目>,...      loaddll    DLL の読み込みを表示する
//                               unresolved 読み込み時にスタブにしたインポートを表示する
//                               relay      DLL の境界を越える呼び出しを引数と戻り値付きで表示する
//   NTMAC_RELAY_EXCLUDE=<名前>,... relay の対象から除く DLL ("ntdll") または関数 ("ntdll.RtlAllocateHeap")
//   NTMAC_UNIMPLEMENTED=<動作>   abort   (既定) 未実装の関数が呼ばれたら表示して終了する
//                               return0 表示して 0 を返す (初回のみ表示)
//                               strict  読み込み時に解決できないインポートがあればエラーにする

namespace ntmac::runtime {

enum class TraceChannel { LoadDll, Unresolved, Relay };

void init_trace(const loader::ModuleManager* modules);
bool trace_enabled(TraceChannel channel);

/**
 * stderr に1行出力します (スレッド間で行が混ざらないようにする)．
 */
void trace_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));

/**
 * アドレスを "module.dll+0x1234" の形で表します．
 */
std::string describe_address(const void* p);

/**
 * 解決できないインポートの代わりに使うスタブを返します．strict モードでは nullptr を返します．
 */
void* make_unimplemented_stub(std::string_view dll, std::string_view symbol);

/**
 * relay が有効で除外されていなければ，target を記録付きで呼ぶ入口を返します．そうでなければ target を返します．
 */
void* wrap_relay(std::string_view dll, std::string_view symbol, void* target);

}
