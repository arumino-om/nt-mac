#include "host/ntmachost/io.h"
#include "host/ntmachost/builtins.h"
#include "shared/ntmachost.h"

namespace ntmac::ntmachost {

namespace {

NTMAC_BUILTIN ntmac_ntstatus ntmachost_call(uint32_t code, void* args) {
    switch (code) {
    case NTMACHOST_WRITE_FILE: {
        auto a = static_cast<ntmachost_io_args*>(args);
        return ntmachost::write_file(a->Handle, a->Buffer, a->Length, &a->Transferred);
    }
    case NTMACHOST_READ_FILE: {
        auto a = static_cast<ntmachost_io_args*>(args);
        return ntmachost::read_file(a->Handle, a->Buffer, a->Length, &a->Transferred);
    }
    case NTMACHOST_EXIT_PROCESS:
        ntmachost::exit_process(static_cast<ntmachost_exit_args*>(args)->ExitCode);
    default:
        return NTMAC_STATUS_INVALID_SYSTEM_SERVICE;
    }
}

}

std::span<const BuiltinExport> ntmachost_exports() {
    static const BuiltinExport exports[] = {
        {NTMACHOST_ENTRY_NAME, reinterpret_cast<void*>(&ntmachost_call)},
    };
    return exports;
}

}
