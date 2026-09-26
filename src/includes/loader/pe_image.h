#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "formats/pe.h"
#include "loader/readutils.h"

namespace ntmac::loader {

class PeImage {
public:
    /**
     * PEイメージを解析し，取り扱えるようにします．
     */
    PeImage(std::vector<uint8_t> buf);

    PeImage(const PeImage&) = delete;
    PeImage& operator=(const PeImage&) = delete;
    ~PeImage() = default;

    const formats::FileHeader& file_header() const { return file_header_; }
    const formats::OptionalHeader64& optional_header() const { return opt_; }
    std::span<const formats::SectionHeader> sections() const { return sections_; }
    static std::string section_name(const formats::SectionHeader& s);
    std::span<const uint8_t> data() const { return data_; }

    /**
     * 指定したインデックスのデータディレクトリを返します．存在しない場合は空のエントリを返します．
     */
    formats::DataDirectory data_directory(size_t index) const {
        if (index >= opt_.NumberOfRvaAndSizes) return {};
        return opt_.DataDirectory[index];
    }

    void print_headers() const;
    void print_sections() const;

    std::optional<size_t> rva_to_offset(uint32_t rva, uint32_t n = 1) const;
    std::optional<std::string> read_cstring_at_rva(uint32_t rva, size_t max_len = 256) const;

    template <class T>
    bool read_at_rva(uint32_t rva, T& out) const {
        auto res = rva_to_offset(rva, sizeof(T));
        if (!res) return false;
        return read_struct(data_, *res, out);
    }

private:
    void parse_headers();
    void parse_sections();

    std::vector<uint8_t> data_;
    size_t nt_offset_ = 0;
    formats::DosHeader dos_{};
    formats::FileHeader file_header_{};
    formats::OptionalHeader64 opt_{};
    std::vector<formats::SectionHeader> sections_;
};

}