#pragma once
#include <cstdint>

namespace ntmac {

constexpr uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) / a * a;
}

constexpr bool is_pow2(uint64_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

}
