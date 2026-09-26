#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include "host/ntmachost/handles.h"
#include "host/ntmachost/io.h"

namespace ntmac::ntmachost {

ntmac_ntstatus write_file(uint64_t handle, const void* buf, uint32_t len, uint32_t* written) {
    *written = 0;
    auto fd = fd_from_handle(handle);
    if (!fd) return NTMAC_STATUS_INVALID_HANDLE;

    auto p = static_cast<const uint8_t*>(buf);
    while (*written < len) {
        ssize_t n = ::write(*fd, p + *written, len - *written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return NTMAC_STATUS_UNSUCCESSFUL;
        }
        *written += static_cast<uint32_t>(n);
    }
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus read_file(uint64_t handle, void* buf, uint32_t len, uint32_t* read) {
    *read = 0;
    auto fd = fd_from_handle(handle);
    if (!fd) return NTMAC_STATUS_INVALID_HANDLE;

    ssize_t n;
    do {
        n = ::read(*fd, buf, len);
    } while (n < 0 && errno == EINTR);

    if (n < 0) return NTMAC_STATUS_UNSUCCESSFUL;
    *read = static_cast<uint32_t>(n);
    return NTMAC_STATUS_SUCCESS;
}

void exit_process(uint32_t code) {
    std::exit(static_cast<int>(code));
}

}
