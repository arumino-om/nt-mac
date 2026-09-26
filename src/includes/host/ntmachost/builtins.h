#pragma once
#include <span>
#include <string_view>
#include "host/loader/import_symbol.h"

// 組み込み関数に付ける呼び出し規約 (Windows x64)．PE コードから入口スタブ経由で呼ばれる
#define NTMAC_BUILTIN __attribute__((ms_abi))

namespace ntmac::ntmachost {

/**
 * 組み込み DLL のエクスポート．impl は ms_abi のホスト関数で，PE からは入口スタブ経由で呼ばれる．
 */
struct BuiltinExport {
    std::string_view name;
    void* impl;
};

struct BuiltinModule {
    std::string_view name; // 小文字のDLL名
    std::span<const BuiltinExport> exports;
};

std::span<const BuiltinExport> ntmachost_exports();

/**
 * 組み込み DLL を探します．dll は小文字に正規化済みの名前．見つからなければ nullptr を返します．
 */
const BuiltinModule* find_builtin_module(std::string_view dll);

/**
 * 組み込み DLL からシンボルを探し，PE から呼べる入口スタブのアドレスを返します．見つからなければ nullptr を返します．
 */
void* resolve_builtin_export(const BuiltinModule& module, const loader::ImportSymbol& sym);

}
