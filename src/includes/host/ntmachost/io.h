#pragma once
#include <cstdint>
#include "shared/ntmachost.h"

namespace ntmac::ntmachost {

ntmac_ntstatus write_file(uint64_t handle, const void* buf, uint32_t len, uint32_t* written);
ntmac_ntstatus read_file(uint64_t handle, void* buf, uint32_t len, uint32_t* read);
[[noreturn]] void exit_process(uint32_t code);

}
