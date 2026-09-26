#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <vector>
#include "nt/builtins.h"
#include "nt/types.h"

namespace ntmac::nt {

namespace {

thread_local DWORD last_error = 0;

// ハンドル値は NT と同様に4の倍数を使う．現状はファイルディスクリプタのみを保持する
std::vector<int>& handle_table() {
    static std::vector<int> table = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
    return table;
}

HANDLE handle_from_index(size_t index) {
    return reinterpret_cast<HANDLE>((index + 1) * 4);
}

int fd_from_handle(HANDLE h) {
    auto v = reinterpret_cast<uintptr_t>(h);
    if (v == 0 || v % 4 != 0) return -1;
    size_t index = v / 4 - 1;
    auto& table = handle_table();
    return index < table.size() ? table[index] : -1;
}

}

namespace kernel32 {

WINAPI DWORD GetLastError() {
    return last_error;
}

WINAPI void SetLastError(DWORD code) {
    last_error = code;
}

WINAPI HANDLE GetStdHandle(DWORD std_handle) {
    switch (std_handle) {
    case kStdInputHandle:  return handle_from_index(0);
    case kStdOutputHandle: return handle_from_index(1);
    case kStdErrorHandle:  return handle_from_index(2);
    default:
        last_error = kErrorInvalidHandle;
        return kInvalidHandleValue;
    }
}

WINAPI BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD to_write, LPDWORD written, LPVOID overlapped) {
    if (written) *written = 0;
    if (overlapped) {
        last_error = kErrorInvalidParameter;
        return kFalse;
    }

    int fd = fd_from_handle(file);
    if (fd < 0) {
        last_error = kErrorInvalidHandle;
        return kFalse;
    }

    auto p = static_cast<const uint8_t*>(buffer);
    DWORD done = 0;
    while (done < to_write) {
        ssize_t n = ::write(fd, p + done, to_write - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            last_error = kErrorWriteFault;
            if (written) *written = done;
            return kFalse;
        }
        done += static_cast<DWORD>(n);
    }

    if (written) *written = done;
    return kTrue;
}

WINAPI BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD to_read, LPDWORD read, LPVOID overlapped) {
    if (read) *read = 0;
    if (overlapped) {
        last_error = kErrorInvalidParameter;
        return kFalse;
    }

    int fd = fd_from_handle(file);
    if (fd < 0) {
        last_error = kErrorInvalidHandle;
        return kFalse;
    }

    ssize_t n;
    do {
        n = ::read(fd, buffer, to_read);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        last_error = kErrorReadFault;
        return kFalse;
    }

    if (read) *read = static_cast<DWORD>(n);
    return kTrue;
}

[[noreturn]] WINAPI void ExitProcess(UINT exit_code) {
    std::exit(static_cast<int>(exit_code));
}

}

std::span<const BuiltinExport> kernel32_exports() {
    static const BuiltinExport exports[] = {
        {"ExitProcess",  reinterpret_cast<void*>(&kernel32::ExitProcess)},
        {"GetLastError", reinterpret_cast<void*>(&kernel32::GetLastError)},
        {"GetStdHandle", reinterpret_cast<void*>(&kernel32::GetStdHandle)},
        {"ReadFile",     reinterpret_cast<void*>(&kernel32::ReadFile)},
        {"SetLastError", reinterpret_cast<void*>(&kernel32::SetLastError)},
        {"WriteFile",    reinterpret_cast<void*>(&kernel32::WriteFile)},
    };
    return exports;
}

}
