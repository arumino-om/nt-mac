#include <unordered_map>
#include <vector>
#include "host/runtime/host_entry.h"
#include "host/ntmachost/builtins.h"
#include "shared/ntmachost.h"

namespace ntmac::ntmachost {

namespace {

std::span<const BuiltinModule> builtin_modules() {
    static const BuiltinModule modules[] = {
        {NTMACHOST_DLL_NAME, ntmachost_exports()},
    };
    return modules;
}

/**
 * ホスト関数から入口スタブへの対応表．全組み込み関数のスタブを初回にまとめて生成する．
 */
const std::unordered_map<const void*, void*>& entry_table() {
    static const auto table = [] {
        std::vector<void*> impls;
        for (const auto& m : builtin_modules()) {
            for (const auto& e : m.exports) impls.push_back(e.impl);
        }

        auto entries = runtime::make_host_entries(impls);
        std::unordered_map<const void*, void*> t;
        for (size_t i = 0; i < impls.size(); i++) t.emplace(impls[i], entries[i]);
        return t;
    }();
    return table;
}

}

const BuiltinModule* find_builtin_module(std::string_view dll) {
    for (const auto& m : builtin_modules()) {
        if (m.name == dll) return &m;
    }
    return nullptr;
}

void* resolve_builtin_export(const BuiltinModule& module, const loader::ImportSymbol& sym) {
    if (sym.ordinal) return nullptr; // 序数によるインポートは未対応

    for (const auto& e : module.exports) {
        if (e.name == sym.name) return entry_table().at(e.impl);
    }
    return nullptr;
}

}
