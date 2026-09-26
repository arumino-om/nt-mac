#pragma once
#include <span>
#include <string_view>
#include "loader/import_symbol.h"

namespace ntmac::nt {

struct BuiltinExport {
    std::string_view name;
    void* address;
};

std::span<const BuiltinExport> kernel32_exports();

/**
 * 組み込みで実装しているDLLからシンボルを探します．見つからなければnullptrを返します．
 */
void* resolve_builtin(std::string_view dll, const loader::ImportSymbol& sym);

}
