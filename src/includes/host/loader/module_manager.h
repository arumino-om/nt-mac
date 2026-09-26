#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "host/loader/import_symbol.h"
#include "host/loader/mapped_image.h"
#include "host/ntmachost/builtins.h"
#include "shared/teb.h"

namespace ntmac::loader {

/**
 * 読み込み済みのモジュール．PE イメージか組み込み DLL のどちらか．
 */
struct Module {
    std::string name;                       // 小文字のファイル名 ("kernel32.dll")
    std::filesystem::path path;             // 組み込み DLL では空
    std::unique_ptr<MappedImage> image;     // 組み込み DLL では nullptr
    const ntmachost::BuiltinModule* builtin = nullptr;
    bool initialized = false;
};

/**
 * プロセス内のモジュールを管理します．依存する DLL の読み込み，インポート・フォワーダの解決，DllMain の呼び出しを行います．
 */
class ModuleManager {
public:
    /**
     * @param search_paths DLL を探すディレクトリ (前にあるものほど優先)
     */
    explicit ModuleManager(std::vector<std::filesystem::path> search_paths);

    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /**
     * 実行ファイルと，それが依存する DLL をすべて読み込みます．DllMain はまだ呼びません．
     */
    Module& load_executable(const std::filesystem::path& path);

    /**
     * DLL を読み込みます．既に読み込み済みならそれを返します．
     * NTMACHOST.DLL は組み込みを使い，それ以外は探索パスから PE の DLL を探します．
     */
    Module& load_library(std::string_view name);

    /**
     * 読み込み済みでまだ初期化していない DLL の DllMain を，依存される側から順に呼びます．
     */
    void initialize(ntmac_teb* teb);

    /**
     * アドレスを含むモジュールを返します．見つからなければ nullptr を返します．
     */
    const Module* find_by_address(const void* p) const;

private:
    Module& load_pe(std::string name, const std::filesystem::path& path, bool expect_dll);
    void* resolve_import(std::string_view dll, const ImportSymbol& sym);
    void* resolve_export(Module& module, const ImportSymbol& sym, int depth);
    std::filesystem::path find_dll_file(const std::string& name) const;
    Module* find_loaded(const std::string& name) const;

    std::vector<std::filesystem::path> search_paths_;
    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<Module*> init_order_;   // 依存される側が先
};

}
