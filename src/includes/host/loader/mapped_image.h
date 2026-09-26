#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include "host/loader/import_symbol.h"
#include "host/loader/pe_image.h"
#include "host/loader/exceptions/load_error.h"

namespace ntmac::loader {

/**
 * エクスポートの検索結果．アドレスか，フォワーダ ("DLL名.関数名" / "DLL名.#序数") のどちらか．
 */
struct ExportTarget {
    void* address = nullptr;
    std::string forwarder;
};

/**
 * メモリ上に展開されたPEイメージ．
 *
 * 構築時に配置と再配置までを行います．続けて resolve_imports() と protect() を呼ぶと実行可能な状態になります．
 * 構築後にpeを破棄しても構いません．
 */
class MappedImage {
public:
    explicit MappedImage(const PeImage& pe);

    MappedImage(const MappedImage&) = delete;
    MappedImage& operator=(const MappedImage&) = delete;
    ~MappedImage();

    /**
     * インポートを解決して IAT に書き込みます．解決できないものがあれば LoadError を投げます．
     */
    void resolve_imports(const ImportResolver& resolver);

    /**
     * セクションの属性に従ってメモリ保護を設定します．
     */
    void protect();

    std::optional<ExportTarget> find_export(const ImportSymbol& sym) const;

    uint8_t* base() const { return base_; }
    size_t size() const { return size_; }
    bool contains(const void* p) const { return p >= base_ && p < base_ + size_; }
    bool is_dll() const { return is_dll_; }
    const formats::OptionalHeader64& optional_header() const { return opt_; }

    /**
     * エントリポイントのアドレス．無ければ nullptr を返します．
     */
    void* entry_point() const { return opt_.AddressOfEntryPoint ? base_ + opt_.AddressOfEntryPoint : nullptr; }

private:
    void reserve(const PeImage& pe);
    void copy_sections(const PeImage& pe);
    void apply_relocations(const PeImage& pe);

    std::optional<uint32_t> export_index_by_name(const formats::ExportDirectory& dir, const ImportSymbol& sym) const;

    template <class T>
    T read(uint64_t rva) const {
        T out;
        std::memcpy(&out, checked(rva, sizeof(T)), sizeof(T));
        return out;
    }

    template <class T>
    void write(uint64_t rva, const T& val) {
        std::memcpy(checked(rva, sizeof(T)), &val, sizeof(T));
    }

    uint8_t* checked(uint64_t rva, uint64_t n) const {
        if (rva > size_ || n > size_ - rva) {
            throw LoadError("イメージ範囲外のRVAを参照しました");
        }
        return base_ + rva;
    }

    std::string read_cstring(uint64_t rva) const;

    uint8_t* base_ = nullptr;
    size_t size_ = 0;
    bool is_dll_ = false;
    formats::OptionalHeader64 opt_{};
    formats::DataDirectory import_dir_{};
    formats::DataDirectory export_dir_{};
    std::vector<formats::SectionHeader> sections_;
};

}
