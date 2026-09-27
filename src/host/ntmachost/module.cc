#include <algorithm>
#include "host/loader/exceptions/load_error.h"
#include "host/loader/exceptions/pe_format_error.h"
#include "host/loader/module_manager.h"
#include "host/ntmachost/services.h"
#include "host/runtime/process_env.h"
#include "host/runtime/unicode.h"

// GetModuleHandle / GetProcAddress / LoadLibrary などを，ホストのローダ (ModuleManager) で処理する

namespace ntmac::ntmachost {

namespace {

loader::ModuleManager& manager() {
    return *loader::ModuleManager::current();
}

bool is_path(std::u16string_view name) {
    return name.find_first_of(u"\\/") != std::u16string_view::npos;
}

loader::Module* module_from_base(uint64_t base) {
    return manager().find_by_base(reinterpret_cast<const void*>(base));
}

}

ntmac_ntstatus get_module_handle(ntmachost_module_args* args) {
    auto name = runtime::utf16_view(args->Name);
    loader::Module* m = nullptr;
    if (name.empty()) {
        // exe は PEB からも分かるが，ここでも返せるようにしておく
        auto peb = runtime::current_teb()->ProcessEnvironmentBlock;
        args->Module = reinterpret_cast<uint64_t>(peb->ImageBaseAddress);
        return NTMAC_STATUS_SUCCESS;
    }

    auto utf8 = runtime::utf16_to_utf8(name);
    m = manager().find_loaded_library(utf8);
    if (!m || !m->image) return NTMAC_STATUS_DLL_NOT_FOUND; // 組み込み (NTMACHOST) はハンドルを持たない
    args->Module = reinterpret_cast<uint64_t>(m->image->base());
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus get_proc_address(ntmachost_module_args* args) {
    auto m = module_from_base(args->Module);
    if (!m) return NTMAC_STATUS_DLL_NOT_FOUND;

    loader::ImportSymbol sym;
    if (args->ProcName) {
        sym.name = args->ProcName;
    } else {
        sym.ordinal = static_cast<uint16_t>(args->Ordinal);
    }

    try {
        args->Address = manager().get_proc_address(*m, sym);
    } catch (const std::runtime_error&) {
        args->Address = nullptr; // フォワーダの先の DLL が無いなど
    }
    return args->Address ? NTMAC_STATUS_SUCCESS : NTMAC_STATUS_PROCEDURE_NOT_FOUND;
}

ntmac_ntstatus get_module_file_name(ntmachost_module_args* args) {
    std::filesystem::path path;
    if (args->Module == 0) {
        auto peb = runtime::current_teb()->ProcessEnvironmentBlock;
        auto m = module_from_base(reinterpret_cast<uint64_t>(peb->ImageBaseAddress));
        if (m) path = m->path;
    } else if (auto m = module_from_base(args->Module)) {
        path = m->path;
    } else {
        return NTMAC_STATUS_DLL_NOT_FOUND;
    }

    auto windows_path = runtime::to_windows_path(path);
    if (args->BufferLength == 0) return NTMAC_STATUS_BUFFER_TOO_SMALL;

    // 入り切らなければ切り詰めて終端を付ける (GetModuleFileName と同じ)
    size_t n = std::min<size_t>(windows_path.size(), args->BufferLength - 1);
    std::copy_n(windows_path.data(), n, reinterpret_cast<char16_t*>(args->Buffer));
    args->Buffer[n] = 0;
    args->BufferLength = static_cast<uint32_t>(n);
    return n < windows_path.size() ? NTMAC_STATUS_BUFFER_TOO_SMALL : NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus load_library(ntmachost_module_args* args) {
    auto name = runtime::utf16_view(args->Name);
    if (name.empty()) return NTMAC_STATUS_INVALID_PARAMETER;

    auto& mgr = manager();
    std::lock_guard guard(mgr.lock());
    size_t first_new = mgr.loaded_count();
    try {
        loader::Module* m;
        if (is_path(name)) {
            auto path = runtime::from_windows_path(name);
            if (path.empty() || !std::filesystem::exists(path)) {
                // パスで見つからなければ名前で探す (C:\Windows\System32\foo.dll のような指定もあるため)
                m = &mgr.load_library(runtime::utf16_to_utf8(name));
            } else {
                m = &mgr.load_library_file(path);
            }
        } else {
            m = &mgr.load_library(runtime::utf16_to_utf8(name));
        }

        // 新しく読み込んだ DLL だけの TLS と DllMain (ホストの処理の途中から PE を呼ぶ)．
        // DllMain の中からの LoadLibrary では，呼び出し元自身の初期化はまだ終わっていないので，
        // 既に読み込み済みで初期化待ちのモジュールには触れない
        mgr.setup_thread_tls(runtime::current_teb());
        mgr.initialize(runtime::current_teb(), first_new);

        if (!m->image) return NTMAC_STATUS_DLL_NOT_FOUND;
        args->Module = reinterpret_cast<uint64_t>(m->image->base());
        return NTMAC_STATUS_SUCCESS;
    } catch (const loader::DllInitError&) {
        return NTMAC_STATUS_DLL_INIT_FAILED;
    } catch (const loader::LoadError&) {
        return NTMAC_STATUS_DLL_NOT_FOUND;
    } catch (const loader::PeFormatError&) {
        return NTMAC_STATUS_DLL_NOT_FOUND;
    }
}

ntmac_ntstatus pc_to_module(ntmachost_module_args* args) {
    auto m = manager().find_by_address(args->Address);
    if (!m) return NTMAC_STATUS_DLL_NOT_FOUND;
    args->Module = reinterpret_cast<uint64_t>(m->image->base());
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus shutdown_process() {
    manager().shutdown(runtime::current_teb());
    return NTMAC_STATUS_SUCCESS;
}

}
