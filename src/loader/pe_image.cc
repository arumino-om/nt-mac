#include <cstring>
#include <iostream>
#include <tuple>
#include "loader/pe_image.h"
#include "loader/exceptions/pe_format_error.h"
#include "purefuncs.h"

namespace ntmac::loader {

namespace {

// "PE\0\0" シグネチャの直後にFileHeaderが続く
constexpr size_t kNtSignatureSize = sizeof(uint32_t);

}

PeImage::PeImage(std::vector<uint8_t> buf) : data_(std::move(buf)) {
    parse_headers();
    parse_sections();
}

void PeImage::parse_headers() {
    if (!(read_struct(data_, 0, dos_) &&
          dos_.e_magic == formats::kDosMagic && dos_.e_lfanew >= 0)) {
        throw PeFormatError("PE形式ではありません");
    }

    uint32_t signature;
    nt_offset_ = dos_.e_lfanew;
    if (!(read_struct(data_, nt_offset_, signature) && signature == formats::kNtSignature)) {
        throw PeFormatError("NTシグネチャが正しくありません");
    }

    if (!(read_struct(data_, nt_offset_ + kNtSignatureSize, file_header_) &&
          file_header_.Machine == formats::kMachineAmd64 &&
          file_header_.SizeOfOptionalHeader >= sizeof(formats::OptionalHeader64))) {
        throw PeFormatError("AMD64以外のアーキテクチャには対応していません");
    }

    if (!(read_struct(data_, nt_offset_ + kNtSignatureSize + sizeof(formats::FileHeader), opt_) &&
          opt_.Magic == formats::kOptionalMagicPe32Plus)) {
        throw PeFormatError("正しいオプショナルヘッダーではありません");
    }

    if (!is_pow2(opt_.SectionAlignment)) {
        throw PeFormatError("SectionAlignmentが2のべき乗の数ではありません");
    }

    if (!is_pow2(opt_.FileAlignment)) {
        throw PeFormatError("FileAlignmentが2のべき乗の数ではありません");
    }

    opt_.NumberOfRvaAndSizes = std::min<uint32_t>(opt_.NumberOfRvaAndSizes, formats::kNumDataDirectories);
}

std::string PeImage::section_name(const formats::SectionHeader& s) {
    auto len = strnlen(reinterpret_cast<const char*>(s.Name), sizeof(s.Name));
    return std::string(s.Name, s.Name + len);
}

void PeImage::parse_sections() {
    auto table_offset = nt_offset_ + kNtSignatureSize + sizeof(formats::FileHeader) + file_header_.SizeOfOptionalHeader;
    auto num_sections = file_header_.NumberOfSections;

    if (table_offset + sizeof(formats::SectionHeader) * num_sections > opt_.SizeOfHeaders) {
        throw PeFormatError("セクションテーブルが事前定義されたサイズを超えています");
    }

    uint64_t prev_end = 0;
    for (uint16_t i = 0; i < num_sections; i++) {
        formats::SectionHeader header;
        if (!read_struct(data_, table_offset + i * sizeof(formats::SectionHeader), header)) {
            throw PeFormatError("セクションヘッダを読み取れませんでした");
        }

        if (header.VirtualAddress < prev_end) {
            throw PeFormatError("セクションが重なっています");
        }

        if (header.SizeOfRawData != 0 &&
            (header.PointerToRawData > data_.size() || header.SizeOfRawData > data_.size() - header.PointerToRawData)) {
            throw PeFormatError("ファイル上の範囲がファイルに収まっていません");
        }

        uint64_t end = static_cast<uint64_t>(header.VirtualAddress) + formats::virtual_size(header);
        if (end > opt_.SizeOfImage) {
            throw PeFormatError("メモリ上の範囲がイメージサイズに収まっていません");
        }

        if (header.VirtualAddress % opt_.SectionAlignment != 0) {
            throw PeFormatError("仮想アドレスがSectionAlignmentの倍数ではありません");
        }

        sections_.push_back(header);
        prev_end = align_up(end, opt_.SectionAlignment);
    }
}

void PeImage::print_headers() const {
    const std::tuple<const char*, uint64_t> values[] = {
        {"Machine", file_header_.Machine},
        {"NumberOfSections", file_header_.NumberOfSections},
        {"Characteristics", file_header_.Characteristics},
        {"AddressOfEntryPoint", opt_.AddressOfEntryPoint},
        {"ImageBase", opt_.ImageBase},
        {"SectionAlignment", opt_.SectionAlignment},
        {"SizeOfImage", opt_.SizeOfImage},
        {"SizeOfHeaders", opt_.SizeOfHeaders},
        {"SubSystem", opt_.Subsystem},
        {"DllCharacteristics", opt_.DllCharacteristics}
    };

    std::cout << "--- NT HEADER ---" << std::endl << std::hex;
    for (const auto& [name, val] : values) {
        std::cout << name << ": " << val << std::endl;
    }

    std::cout << std::endl;
    for (uint32_t i = 0; i < opt_.NumberOfRvaAndSizes; i++) {
        if (opt_.DataDirectory[i].Size == 0) continue;
        std::cout << "DataDirectory " << i << ": " << opt_.DataDirectory[i].VirtualAddress << std::endl;
    }
    std::cout << std::dec;
}

void PeImage::print_sections() const {
    std::cout << std::hex;
    for (const auto& sec : sections_) {
        const std::tuple<const char*, uint64_t> values[] = {
            {"VirtualAddress", sec.VirtualAddress},
            {"VirtualSize", sec.VirtualSize},
            {"PointerToRawData", sec.PointerToRawData},
            {"SizeofRawData", sec.SizeOfRawData},
        };

        std::cout << "Name: " << section_name(sec) << std::endl;
        for (const auto& [name, val] : values) {
            std::cout << name << ": " << val << std::endl;
        }

        std::cout << "Characteristics: ";
        if (sec.Characteristics & formats::kScnMemRead) std::cout << "R ";
        if (sec.Characteristics & formats::kScnMemWrite) std::cout << "W ";
        if (sec.Characteristics & formats::kScnMemExecute) std::cout << "X ";
        std::cout << std::endl << "--" << std::endl;
    }
    std::cout << std::dec;
}

std::optional<size_t> PeImage::rva_to_offset(uint32_t rva, uint32_t n) const {
    if (static_cast<uint64_t>(rva) + n <= opt_.SizeOfHeaders) return rva;

    for (const auto& sec : sections_) {
        uint32_t size = formats::virtual_size(sec);
        if (rva < sec.VirtualAddress || rva - sec.VirtualAddress >= size) continue;

        uint64_t d = rva - sec.VirtualAddress;
        if (d + n > size || d + n > sec.SizeOfRawData) return std::nullopt;
        return sec.PointerToRawData + d;
    }

    return std::nullopt;
}

std::optional<std::string> PeImage::read_cstring_at_rva(uint32_t rva, size_t max_len) const {
    auto offset = rva_to_offset(rva);
    if (!offset) return std::nullopt;

    auto str = reinterpret_cast<const char*>(data_.data()) + *offset;
    auto len = strnlen(str, std::min(max_len, data_.size() - *offset));
    return std::string(str, len);
}

}
