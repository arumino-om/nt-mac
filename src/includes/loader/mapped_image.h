#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "loader/import_symbol.h"
#include "loader/pe_image.h"
#include "loader/exceptions/load_error.h"

namespace ntmac::loader {

/**
 * メモリ上に展開され，実行可能な状態になったPEイメージ．
 */
class MappedImage {
public:
    /**
     * PEイメージをメモリに配置し，再配置・インポート解決・メモリ保護の設定までを行います．
     * 構築後にpeを破棄しても構いません．
     */
    MappedImage(const PeImage& pe, const ImportResolver& resolver);

    MappedImage(const MappedImage&) = delete;
    MappedImage& operator=(const MappedImage&) = delete;
    ~MappedImage();

    uint8_t* base() const { return base_; }
    size_t size() const { return size_; }

    /**
     * エントリポイントを呼び出し，その戻り値を返します．
     */
    uint32_t run() const;

private:
    void reserve(const PeImage& pe);
    void copy_sections(const PeImage& pe);
    void apply_relocations(const PeImage& pe);
    void resolve_imports(const PeImage& pe, const ImportResolver& resolver);
    void protect(const PeImage& pe);

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
    uint32_t entry_rva_ = 0;
};

}
