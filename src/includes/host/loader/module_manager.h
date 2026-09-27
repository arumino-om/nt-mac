#pragma once
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

    // 静的 TLS (TLS ディレクトリを持つモジュールのみ)
    std::optional<uint32_t> tls_index;
    const uint8_t* tls_template = nullptr;  // 各スレッドの TLS ブロックの初期値
    size_t tls_template_size = 0;
    size_t tls_zero_fill = 0;
    std::vector<void*> tls_callbacks;
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
    ~ModuleManager();

    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /**
     * プロセスのモジュール管理 (最後に作られたもの) を返します．
     */
    static ModuleManager* current();

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
     * ファイルのパスを指定して DLL を読み込みます．同じ名前の DLL が読み込み済みならそれを返します．
     */
    Module& load_library_file(const std::filesystem::path& path);

    /**
     * 読み込み済みの DLL を名前で探します (読み込みはしない)．見つからなければ nullptr を返します．
     */
    Module* find_loaded_library(std::string_view name);

    /**
     * モジュールのエクスポートを探します (フォワーダも解決する)．見つからなければ nullptr を返します．
     */
    void* get_proc_address(Module& module, const ImportSymbol& sym);

    /**
     * 先頭アドレスからモジュールを探します．
     */
    Module* find_by_base(const void* base);

    /**
     * ローダロック．読み込みや問い合わせの間，他のスレッドを待たせる (同じスレッドからの再入は可)
     */
    std::recursive_mutex& lock() { return lock_; }

    /**
     * 呼び出したスレッドの静的 TLS を確保し，teb->ThreadLocalStoragePointer に設定します．
     */
    void setup_thread_tls(ntmac_teb* teb);

    /**
     * 読み込み済みでまだ初期化していないモジュールの TLS コールバックと DllMain を，依存される側から順に呼びます．
     * from を指定すると，初期化の順序で from 番目以降 (＝その時点より後に読み込んだモジュール) だけを初期化します．
     */
    void initialize(ntmac_teb* teb, size_t from = 0);

    /**
     * プロセスの終了処理．初期化と逆の順に，TLS コールバックと DllMain を DLL_PROCESS_DETACH で呼びます．
     * 2回目以降の呼び出しは何もしません．
     */
    void shutdown(ntmac_teb* teb);

    /**
     * これまでに読み込んだ PE モジュールの数．initialize() の from に使う
     */
    size_t loaded_count() const { return init_order_.size(); }

    /**
     * アドレスを含むモジュールを返します．見つからなければ nullptr を返します．
     */
    const Module* find_by_address(const void* p) const;

private:
    Module& load_pe(std::string name, const std::filesystem::path& path, bool expect_dll);
    void prepare_tls(Module& module);
    void* resolve_import(std::string_view dll, const ImportSymbol& sym);
    void* relay_if_code(std::string_view dll, std::string_view symbol, void* address);
    void* resolve_export(Module& module, const ImportSymbol& sym, int depth);
    std::filesystem::path find_dll_file(const std::string& name) const;
    Module* find_loaded(const std::string& name) const;

    std::vector<std::filesystem::path> search_paths_;
    std::vector<std::unique_ptr<Module>> modules_;
    std::vector<Module*> init_order_;   // 依存される側が先
    uint32_t tls_count_ = 0;
    bool shutting_down_ = false;
    std::unordered_map<const ntmac_teb*, size_t> tls_allocated_;   // スレッドごとに確保済みの TLS の数
    std::recursive_mutex lock_;
};

}
