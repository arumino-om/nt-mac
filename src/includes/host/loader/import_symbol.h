#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ntmac::loader {

/**
 * インポートされるシンボル．名前によるインポートか序数によるインポートのどちらか．
 */
struct ImportSymbol {
    std::string name;
    uint16_t hint = 0;
    std::optional<uint16_t> ordinal;

    std::string describe() const {
        return ordinal ? "#" + std::to_string(*ordinal) : name;
    }
};

/**
 * DLL名とシンボルから関数のアドレスを返します．見つからなければnullptrを返します．
 */
using ImportResolver = std::function<void*(std::string_view dll, const ImportSymbol& sym)>;

}
