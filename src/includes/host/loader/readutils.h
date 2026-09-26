#pragma once
#include <vector>
#include <span>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace ntmac::loader {

template <class T>
bool read_struct(std::span<const uint8_t> buf, size_t offset, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);

    // 読み取り時にbufの境界を越えてしまう場合は，何もしない
    if (offset > buf.size() || sizeof(T) > buf.size() - offset) {
        return false;
    }

    // 読み取り
    std::memcpy(&out, buf.data() + offset, sizeof(T));
    return true;
}

bool read_file(const char* path, std::vector<uint8_t>& out);
}