#include <algorithm>
#include <cctype>
#include "host/runtime/thunks.h"
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

}

ModuleManager::ModuleManager(std::vector<std::filesystem::path> search_paths)
    : search_paths_(std::move(search_paths)) {}

Module& ModuleManager::load_executable(const std::filesystem::path& path) {
    return load_pe(normalize_dll_name(path.filename().string()), path, false);
}

Module& ModuleManager::load_library(std::string_view name) {
    auto key = normalize_dll_name(name);
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
        module.image->protect();

        init_order_.push_back(&module);
        return module;
    } catch (const std::runtime_error& e) {
        throw LoadError(name + " の読み込みに失敗しました: " + e.what());
    }
}

void* ModuleManager::resolve_import(std::string_view dll, const ImportSymbol& sym) {
    return resolve_export(load_library(dll), sym, 0);
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

void ModuleManager::initialize(ntmac_teb* teb) {
    for (auto m : init_order_) {
        if (m->initialized || !m->image->is_dll()) continue;
        m->initialized = true;

        auto entry = m->image->entry_point();
        if (!entry) continue;

        // DllMain(hinstDLL, DLL_PROCESS_ATTACH, lpReserved)．静的な読み込みでは lpReserved は非NULL
        auto ok = static_cast<uint32_t>(ntmac_call_pe(teb, entry, reinterpret_cast<uint64_t>(m->image->base()),
                                                      formats::kDllProcessAttach, 1));
        if (!ok) {
            throw LoadError(m->name + " の DllMain が失敗しました");
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
