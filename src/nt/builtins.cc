#include <algorithm>
#include <cctype>
#include <string>
#include "nt/builtins.h"

namespace ntmac::nt {

namespace {

struct BuiltinModule {
    std::string_view name; // 小文字のDLL名
    std::span<const BuiltinExport> exports;
};

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

const BuiltinModule* find_module(std::string_view dll) {
    static const BuiltinModule modules[] = {
        {"kernel32.dll", kernel32_exports()},
    };

    auto name = to_lower(dll);
    for (const auto& m : modules) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

}

void* resolve_builtin(std::string_view dll, const loader::ImportSymbol& sym) {
    auto module = find_module(dll);
    if (!module || sym.ordinal) return nullptr; // 序数によるインポートは未対応

    for (const auto& e : module->exports) {
        if (e.name == sym.name) return e.address;
    }
    return nullptr;
}

}
