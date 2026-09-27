#pragma once
#include <cstdint>
#include "shared/ntmachost.h"

// NTMACHOST.DLL が提供するサービスのホスト側の実装

namespace ntmac::ntmachost {

// io.cc
ntmac_ntstatus write_file(uint64_t handle, const void* buf, uint32_t len, uint32_t* written);
ntmac_ntstatus read_file(uint64_t handle, void* buf, uint32_t len, uint32_t* read);
ntmac_ntstatus query_file_type(ntmachost_handle_args* args);
ntmac_ntstatus close_handle(uint64_t handle);

// memory.cc
ntmac_ntstatus allocate_virtual_memory(ntmachost_vm_args* args);
ntmac_ntstatus free_virtual_memory(ntmachost_vm_args* args);
ntmac_ntstatus protect_virtual_memory(ntmachost_vm_args* args);
ntmac_ntstatus query_virtual_memory(ntmachost_vm_query_args* args);

// process.cc
[[noreturn]] void exit_process(uint32_t code);
ntmac_ntstatus delay_execution(int64_t interval);
ntmac_ntstatus yield_execution();
ntmac_ntstatus query_system_time(ntmachost_time_args* args);
ntmac_ntstatus query_performance_counter(ntmachost_perf_args* args);

// sync.cc
ntmac_ntstatus wait_on_address(const ntmachost_wait_args* args);
ntmac_ntstatus wake_by_address(const ntmachost_wake_args* args);

// module.cc
ntmac_ntstatus get_module_handle(ntmachost_module_args* args);
ntmac_ntstatus get_proc_address(ntmachost_module_args* args);
ntmac_ntstatus get_module_file_name(ntmachost_module_args* args);
ntmac_ntstatus load_library(ntmachost_module_args* args);
ntmac_ntstatus pc_to_module(ntmachost_module_args* args);
ntmac_ntstatus shutdown_process();

}
