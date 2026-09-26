#include <unistd.h>
#include <vector>
#include "host/ntmachost/handles.h"

namespace ntmac::ntmachost {

namespace {

std::vector<int>& handle_table() {
    static std::vector<int> table = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
    return table;
}

uint64_t handle_from_index(size_t index) {
    return (index + 1) * 4;
}

}

uint64_t std_handle(int fd) {
    return handle_from_index(static_cast<size_t>(fd));
}

std::optional<int> fd_from_handle(uint64_t handle) {
    if (handle == 0 || handle % 4 != 0) return std::nullopt;
    size_t index = handle / 4 - 1;
    auto& table = handle_table();
    if (index >= table.size()) return std::nullopt;
    return table[index];
}

}
