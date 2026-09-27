#include "host/ntmachost/builtins.h"
#include "host/ntmachost/services.h"
#include "host/win32k/services.h"
#include "shared/ntmachost.h"
#include "shared/ntmachost_user.h"

namespace ntmac::ntmachost {

namespace {

template <class T>
T* as(void* args) {
    return static_cast<T*>(args);
}

NTMAC_BUILTIN ntmac_ntstatus ntmachost_call(uint32_t code, void* args) {
    switch (code) {
    case NTMACHOST_WRITE_FILE: {
        auto a = as<ntmachost_io_args>(args);
        return write_file(a->Handle, a->Buffer, a->Length, &a->Transferred);
    }
    case NTMACHOST_READ_FILE: {
        auto a = as<ntmachost_io_args>(args);
        return read_file(a->Handle, a->Buffer, a->Length, &a->Transferred);
    }
    case NTMACHOST_EXIT_PROCESS:
        exit_process(as<ntmachost_exit_args>(args)->ExitCode);
    case NTMACHOST_ALLOCATE_VIRTUAL_MEMORY:
        return allocate_virtual_memory(as<ntmachost_vm_args>(args));
    case NTMACHOST_FREE_VIRTUAL_MEMORY:
        return free_virtual_memory(as<ntmachost_vm_args>(args));
    case NTMACHOST_PROTECT_VIRTUAL_MEMORY:
        return protect_virtual_memory(as<ntmachost_vm_args>(args));
    case NTMACHOST_QUERY_VIRTUAL_MEMORY:
        return query_virtual_memory(as<ntmachost_vm_query_args>(args));
    case NTMACHOST_DELAY_EXECUTION:
        return delay_execution(as<ntmachost_delay_args>(args)->Interval);
    case NTMACHOST_YIELD_EXECUTION:
        return yield_execution();
    case NTMACHOST_QUERY_SYSTEM_TIME:
        return query_system_time(as<ntmachost_time_args>(args));
    case NTMACHOST_QUERY_PERFORMANCE_COUNTER:
        return query_performance_counter(as<ntmachost_perf_args>(args));
    case NTMACHOST_QUERY_FILE_TYPE:
        return query_file_type(as<ntmachost_handle_args>(args));
    case NTMACHOST_CLOSE_HANDLE:
        return close_handle(as<ntmachost_handle_args>(args)->Handle);
    case NTMACHOST_WAIT_ON_ADDRESS:
        return wait_on_address(as<ntmachost_wait_args>(args));
    case NTMACHOST_WAKE_BY_ADDRESS:
        return wake_by_address(as<ntmachost_wake_args>(args));
    case NTMACHOST_GET_MODULE_HANDLE:
        return get_module_handle(as<ntmachost_module_args>(args));
    case NTMACHOST_GET_PROC_ADDRESS:
        return get_proc_address(as<ntmachost_module_args>(args));
    case NTMACHOST_GET_MODULE_FILE_NAME:
        return get_module_file_name(as<ntmachost_module_args>(args));
    case NTMACHOST_LOAD_LIBRARY:
        return load_library(as<ntmachost_module_args>(args));
    case NTMACHOST_PC_TO_MODULE:
        return pc_to_module(as<ntmachost_module_args>(args));
    case NTMACHOST_SHUTDOWN_PROCESS:
        return shutdown_process();
    default:
        if (code >= NTMACHOST_USER_REGISTER_CLASS) return win32k::dispatch(code, args);
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
