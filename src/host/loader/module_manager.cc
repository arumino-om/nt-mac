#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include "host/runtime/thunks.h"
#include "host/runtime/trace.h"
#include "host/loader/exceptions/load_error.h"
#include "host/loader/module_manager.h"
#include "host/loader/pe_image.h"
#include "host/loader/readutils.h"
#include "shared/ntmachost.h"

namespace ntmac::loader {

namespace {

// フォワーダが循環していた場合に備えた上限
constexpr int kMaxForwardDepth = 16;

/**
 * DLL 名を正規化します．パスを除き，小文字にし，拡張子が無ければ ".dll" を付けます．
 */
std::string normalize_dll_name(std::string_view name) {
    auto slash = name.find_last_of("\\/");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);

    std::string out(name);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    if (out.find('.') == std::string::npos) out += ".dll";
    return out;
}

/**
 * API set (api-ms-win-*.dll) の名前を実際の DLL の名前に置き換えます．
 * Windows では PEB の ApiSetMap で解決されるが，ここでは接頭辞で対応付ける．
 */
std::string resolve_api_set(std::string name) {
    // 前にあるものほど優先 (より長い接頭辞を先に書く)．
    // Windows では core の多くは kernelbase.dll に向くが，ここでは kernel32.dll にまとめている
    static const std::pair<std::string_view, std::string_view> api_sets[] = {
        {"api-ms-win-crt-", "ucrtbase.dll"},
        {"api-ms-win-core-rtlsupport-", "ntdll.dll"},
        {"api-ms-win-core-", "kernel32.dll"},
    };
    for (const auto& [prefix, dll] : api_sets) {
        if (name.starts_with(prefix)) return std::string(dll);
    }
    return name;
}

}

namespace {
std::atomic<ModuleManager*> current_manager{nullptr};
}

ModuleManager::ModuleManager(std::vector<std::filesystem::path> search_paths)
    : search_paths_(std::move(search_paths)) {
    current_manager = this;
}

ModuleManager::~ModuleManager() {
    ModuleManager* self = this;
    current_manager.compare_exchange_strong(self, nullptr);
}

ModuleManager* ModuleManager::current() {
    return current_manager;
}

Module& ModuleManager::load_executable(const std::filesystem::path& path) {
    std::lock_guard guard(lock_);
    return load_pe(normalize_dll_name(path.filename().string()), path, false);
}

Module& ModuleManager::load_library_file(const std::filesystem::path& path) {
    std::lock_guard guard(lock_);
    auto key = normalize_dll_name(path.filename().string());
    if (auto m = find_loaded(key)) return *m;
    return load_pe(key, path, true);
}

Module* ModuleManager::find_loaded_library(std::string_view name) {
    std::lock_guard guard(lock_);
    return find_loaded(resolve_api_set(normalize_dll_name(name)));
}

void* ModuleManager::get_proc_address(Module& module, const ImportSymbol& sym) {
    std::lock_guard guard(lock_);
    void* address = resolve_export(module, sym, 0);
    return address ? relay_if_code(module.name, sym.describe(), address) : nullptr;
}

Module* ModuleManager::find_by_base(const void* base) {
    std::lock_guard guard(lock_);
    for (const auto& m : modules_) {
        if (m->image && m->image->base() == base) return m.get();
    }
    return nullptr;
}

Module& ModuleManager::load_library(std::string_view name) {
    std::lock_guard guard(lock_);
    auto key = resolve_api_set(normalize_dll_name(name));
    if (auto m = find_loaded(key)) return *m;

    auto builtin = ntmachost::find_builtin_module(key);
    if (!builtin) {
        auto path = find_dll_file(key);
        if (path.empty()) throw LoadError("DLL が見つかりません: " + std::string(name));
        return load_pe(key, path, true);
    }

    auto m = std::make_unique<Module>();
    m->name = key;
    m->builtin = builtin;
    m->initialized = true;
    modules_.push_back(std::move(m));
    if (runtime::trace_enabled(runtime::TraceChannel::LoadDll)) {
        runtime::trace_printf("loaddll: %s (組み込み)", key.c_str());
    }
    return *modules_.back();
}

Module& ModuleManager::load_pe(std::string name, const std::filesystem::path& path, bool expect_dll) {
    try {
        std::vector<uint8_t> data;
        if (!read_file(path.c_str(), data)) {
            throw LoadError("ファイルを開けません: " + path.string());
        }

        auto m = std::make_unique<Module>();
        m->name = name;
        m->path = path;
        {
            PeImage pe(std::move(data));
            m->image = std::make_unique<MappedImage>(pe);
        }

        if (m->image->is_dll() != expect_dll) {
            throw LoadError(expect_dll ? "DLL ではありません" : "実行ファイルではありません");
        }

        // 循環参照に備え，インポートを解決する前に登録しておく
        auto& module = *m;
        modules_.push_back(std::move(m));

        module.image->resolve_imports([this](std::string_view dll, const ImportSymbol& sym) {
            return resolve_import(dll, sym);
        });
        prepare_tls(module);    // _tls_index に書き込むので，保護を設定する前に行う
        module.image->protect();

        init_order_.push_back(&module);
        if (runtime::trace_enabled(runtime::TraceChannel::LoadDll)) {
            runtime::trace_printf("loaddll: %s を %p に読み込みました (%s)", module.name.c_str(),
                                  static_cast<void*>(module.image->base()), path.c_str());
        }
        return module;
    } catch (const std::runtime_error& e) {
        throw LoadError(name + " の読み込みに失敗しました: " + e.what());
    }
}

void* ModuleManager::resolve_import(std::string_view dll, const ImportSymbol& sym) {
    auto& module = load_library(dll);
    auto symbol = sym.describe();

    // 見つからない関数はスタブにして読み込みを続け，呼ばれた時点で報告する (strict モードでは nullptr のまま)
    void* address = resolve_export(module, sym, 0);
    if (!address) return runtime::make_unimplemented_stub(module.name, symbol);
    return relay_if_code(module.name, symbol, address);
}

void* ModuleManager::relay_if_code(std::string_view dll, std::string_view symbol, void* address) {
    // データのエクスポート (std::cout など) を包むと，呼び出し側が thunk をデータとして読んでしまう
    auto owner = find_by_address(address);
    bool is_code = owner ? owner->image->is_code(address) : true; // 組み込み DLL の入口は常にコード
    return is_code ? runtime::wrap_relay(dll, symbol, address) : address;
}

void* ModuleManager::resolve_export(Module& module, const ImportSymbol& sym, int depth) {
    if (module.builtin) {
        return ntmachost::resolve_builtin_export(*module.builtin, sym);
    }

    auto target = module.image->find_export(sym);
    if (!target) return nullptr;
    if (target->forwarder.empty()) return target->address;

    if (depth >= kMaxForwardDepth) {
        throw LoadError("フォワーダが深すぎます: " + module.name + "!" + sym.describe());
    }

    // "DLL名.関数名" または "DLL名.#序数"
    const auto& fwd = target->forwarder;
    auto dot = fwd.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= fwd.size()) {
        throw LoadError("フォワーダの形式が不正です: " + fwd);
    }

    ImportSymbol next;
    auto func = fwd.substr(dot + 1);
    if (func[0] == '#') {
        next.ordinal = static_cast<uint16_t>(std::stoul(func.substr(1)));
    } else {
        next.name = func;
    }
    return resolve_export(load_library(fwd.substr(0, dot)), next, depth + 1);
}

void ModuleManager::prepare_tls(Module& module) {
    auto dir = module.image->tls_directory();
    if (!dir) return;

    const auto& image = *module.image;
    uint64_t size = dir->EndAddressOfRawData - dir->StartAddressOfRawData;
    auto index_ptr = image.at_va(dir->AddressOfIndex, sizeof(uint32_t));
    auto tmpl = size ? image.at_va(dir->StartAddressOfRawData, size) : image.base();
    if (dir->EndAddressOfRawData < dir->StartAddressOfRawData || !index_ptr || !tmpl) {
        throw LoadError("TLS ディレクトリが不正です");
    }

    module.tls_index = tls_count_++;
    std::memcpy(index_ptr, &*module.tls_index, sizeof(uint32_t));
    module.tls_template = tmpl;
    module.tls_template_size = size;
    module.tls_zero_fill = dir->SizeOfZeroFill;

    if (dir->AddressOfCallBacks) {
        for (uint64_t va = dir->AddressOfCallBacks;; va += sizeof(uint64_t)) {
            auto p = image.at_va(va, sizeof(uint64_t));
            if (!p) throw LoadError("TLS コールバックの配列が不正です");
            uint64_t callback;
            std::memcpy(&callback, p, sizeof(callback));
            if (callback == 0) break;
            if (!image.at_va(callback, 1)) throw LoadError("TLS コールバックがイメージの外を指しています");
            module.tls_callbacks.push_back(reinterpret_cast<void*>(callback));
        }
    }
}

void ModuleManager::setup_thread_tls(ntmac_teb* teb) {
    std::lock_guard guard(lock_);
    size_t& allocated = tls_allocated_[teb];
    if (allocated == tls_count_) return;

    // LoadLibrary で TLS を持つ DLL が増えた場合は配列を大きくし，増えた分だけブロックを確保する．
    // 古い配列は他から参照されていないが，念のため解放しない (スレッドの終了処理は未実装)
    auto slots = static_cast<void**>(std::calloc(tls_count_, sizeof(void*)));
    if (!slots) throw LoadError("TLS 用のメモリを確保できませんでした");
    if (allocated) std::memcpy(slots, teb->ThreadLocalStoragePointer, allocated * sizeof(void*));

    for (const auto& m : modules_) {
        if (!m->tls_index || *m->tls_index < allocated) continue;
        auto block = static_cast<uint8_t*>(std::calloc(1, m->tls_template_size + m->tls_zero_fill + 1));
        if (!block) throw LoadError("TLS 用のメモリを確保できませんでした");
        std::memcpy(block, m->tls_template, m->tls_template_size);
        slots[*m->tls_index] = block;
    }
    teb->ThreadLocalStoragePointer = slots;
    allocated = tls_count_;
}

void ModuleManager::initialize(ntmac_teb* teb, size_t from) {
    std::lock_guard guard(lock_);
    for (size_t i = from; i < init_order_.size(); i++) {
        auto m = init_order_[i]; // DllMain の中の LoadLibrary で init_order_ が伸びることがあるので添字で回す
        if (m->initialized) continue;
        m->initialized = true;

        auto base = reinterpret_cast<uint64_t>(m->image->base());
        for (auto callback : m->tls_callbacks) {
            ntmac_call_pe(teb, callback, base, formats::kDllProcessAttach, 0);
        }

        auto entry = m->image->entry_point();
        if (!m->image->is_dll() || !entry) continue;

        // DllMain(hinstDLL, DLL_PROCESS_ATTACH, lpReserved)．静的な読み込みでは lpReserved は非NULL
        auto ok = static_cast<uint32_t>(ntmac_call_pe(teb, entry, base, formats::kDllProcessAttach, 1));
        if (!ok) {
            throw DllInitError(m->name + " の DllMain が失敗しました");
        }
    }
}

void ModuleManager::shutdown(ntmac_teb* teb) {
    std::lock_guard guard(lock_);
    if (shutting_down_) return;
    shutting_down_ = true;

    for (auto it = init_order_.rbegin(); it != init_order_.rend(); ++it) {
        auto m = *it;
        if (!m->initialized) continue;
        auto base = reinterpret_cast<uint64_t>(m->image->base());

        // DllMain(hinstDLL, DLL_PROCESS_DETACH, lpReserved)．プロセスの終了時は lpReserved は非NULL
        auto entry = m->image->entry_point();
        if (m->image->is_dll() && entry) ntmac_call_pe(teb, entry, base, formats::kDllProcessDetach, 1);
        for (auto callback : m->tls_callbacks) {
            ntmac_call_pe(teb, callback, base, formats::kDllProcessDetach, 0);
        }
    }
}

const Module* ModuleManager::find_by_address(const void* p) const {
    for (const auto& m : modules_) {
        if (m->image && m->image->contains(p)) return m.get();
    }
    return nullptr;
}

std::filesystem::path ModuleManager::find_dll_file(const std::string& name) const {
    for (const auto& dir : search_paths_) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            // ファイルシステムが大文字小文字を区別する場合にも対応するため，名前を正規化して比べる
            if (entry.is_regular_file(ec) && normalize_dll_name(entry.path().filename().string()) == name) {
                return entry.path();
            }
        }
    }
    return {};
}

Module* ModuleManager::find_loaded(const std::string& name) const {
    for (const auto& m : modules_) {
        if (m->name == name) return m.get();
    }
    return nullptr;
}

}
