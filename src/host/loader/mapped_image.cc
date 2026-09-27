#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include "host/loader/mapped_image.h"
#include "purefuncs.h"

namespace ntmac::loader {

MappedImage::MappedImage(const PeImage& pe)
    : is_dll_(pe.file_header().Characteristics & formats::kFileDll),
      opt_(pe.optional_header()),
      import_dir_(pe.data_directory(formats::kDirImport)),
      export_dir_(pe.data_directory(formats::kDirExport)),
      tls_dir_(pe.data_directory(formats::kDirTls)),
      sections_(pe.sections().begin(), pe.sections().end()) {
    reserve(pe);
    try {
        copy_sections(pe);
        apply_relocations(pe);
    } catch (...) {
        munmap(base_, size_);
        throw;
    }
}

MappedImage::~MappedImage() {
    if (base_) munmap(base_, size_);
}

void MappedImage::reserve(const PeImage& pe) {
    const auto& opt = pe.optional_header();
    size_ = align_up(opt.SizeOfImage, getpagesize());

    // まずは優先ベースアドレスへの配置を試みる (MAP_FIXEDは既存のマッピングを壊すので使わない)
    auto hint = reinterpret_cast<void*>(opt.ImageBase);
    void* p = mmap(hint, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) {
        throw LoadError("イメージ用のメモリを確保できませんでした");
    }
    base_ = static_cast<uint8_t*>(p);

    if (p != hint) {
        bool relocatable = !(pe.file_header().Characteristics & formats::kFileRelocsStripped) &&
                           pe.data_directory(formats::kDirBaseReloc).Size != 0;
        if (!relocatable) {
            munmap(p, size_);
            base_ = nullptr;
            throw LoadError("優先ベースアドレスに配置できず，再配置情報もありません");
        }
    }
}

void MappedImage::copy_sections(const PeImage& pe) {
    auto data = pe.data();
    const auto& opt = pe.optional_header();

    std::memcpy(base_, data.data(), std::min<size_t>(opt.SizeOfHeaders, data.size()));

    // 範囲はPeImage::parse_sectionsで検証済み．SizeOfRawDataを超える部分はmmapによりゼロ埋めされている
    for (const auto& sec : pe.sections()) {
        uint32_t raw = std::min(sec.SizeOfRawData, formats::virtual_size(sec));
        if (raw == 0) continue;
        std::memcpy(base_ + sec.VirtualAddress, data.data() + sec.PointerToRawData, raw);
    }
}

void MappedImage::apply_relocations(const PeImage& pe) {
    int64_t delta = reinterpret_cast<int64_t>(base_) - static_cast<int64_t>(pe.optional_header().ImageBase);
    if (delta == 0) return;

    auto dir = pe.data_directory(formats::kDirBaseReloc);
    uint64_t pos = dir.VirtualAddress;
    uint64_t end = pos + dir.Size;

    while (end - pos >= sizeof(formats::BaseRelocationBlock)) {
        auto block = read<formats::BaseRelocationBlock>(pos);
        if (block.SizeOfBlock < sizeof(block) || block.SizeOfBlock > end - pos) {
            throw LoadError("ベースリロケーションブロックのサイズが不正です");
        }

        size_t count = (block.SizeOfBlock - sizeof(block)) / sizeof(uint16_t);
        for (size_t i = 0; i < count; i++) {
            auto entry = read<uint16_t>(pos + sizeof(block) + i * sizeof(uint16_t));
            uint16_t type = entry >> 12;
            uint64_t target = static_cast<uint64_t>(block.VirtualAddress) + (entry & 0xFFF);

            switch (type) {
            case formats::kRelBasedAbsolute:
                break;
            case formats::kRelBasedDir64:
                write<uint64_t>(target, read<uint64_t>(target) + delta);
                break;
            case formats::kRelBasedHighLow:
                write<uint32_t>(target, read<uint32_t>(target) + static_cast<uint32_t>(delta));
                break;
            default:
                throw LoadError("未対応のリロケーション種別です: " + std::to_string(type));
            }
        }

        pos += block.SizeOfBlock;
    }
}

void MappedImage::resolve_imports(const ImportResolver& resolver) {
    if (import_dir_.Size == 0) return;

    std::vector<std::string> missing;
    for (uint64_t pos = import_dir_.VirtualAddress;; pos += sizeof(formats::ImportDescriptor)) {
        auto desc = read<formats::ImportDescriptor>(pos);
        if (desc.OriginalFirstThunk == 0 && desc.Name == 0 && desc.FirstThunk == 0) break;

        auto dll = read_cstring(desc.Name);
        // ILTが無い場合はIATそのものを名前の参照に使う
        uint64_t lookup = desc.OriginalFirstThunk != 0 ? desc.OriginalFirstThunk : desc.FirstThunk;

        for (uint64_t i = 0;; i++) {
            auto entry = read<uint64_t>(lookup + i * sizeof(uint64_t));
            if (entry == 0) break;

            ImportSymbol sym;
            if (entry & formats::kOrdinalFlag64) {
                sym.ordinal = static_cast<uint16_t>(entry & 0xFFFF);
            } else {
                uint64_t by_name = entry & 0x7FFFFFFF;
                sym.hint = read<uint16_t>(by_name);
                sym.name = read_cstring(by_name + sizeof(uint16_t));
            }

            void* addr = resolver(dll, sym);
            if (!addr) {
                missing.push_back(dll + "!" + sym.describe());
                continue;
            }
            write<uint64_t>(desc.FirstThunk + i * sizeof(uint64_t), reinterpret_cast<uint64_t>(addr));
        }
    }

    if (!missing.empty()) {
        std::string msg = "解決できないインポートがあります:";
        for (const auto& m : missing) msg += "\n  " + m;
        throw LoadError(msg);
    }
}

void MappedImage::protect() {
    const size_t page = getpagesize();

    // ホストのページサイズがSectionAlignmentより大きい場合に備え，ページ単位で保護属性を合成する
    std::vector<int> prot(size_ / page, PROT_NONE);
    auto mark = [&](uint64_t rva, uint64_t len, int flags) {
        if (len == 0) return;
        for (uint64_t p = rva / page; p <= (rva + len - 1) / page && p < prot.size(); p++) {
            prot[p] |= flags;
        }
    };

    mark(0, opt_.SizeOfHeaders, PROT_READ);
    for (const auto& sec : sections_) {
        int flags = 0;
        if (sec.Characteristics & formats::kScnMemRead) flags |= PROT_READ;
        if (sec.Characteristics & formats::kScnMemWrite) flags |= PROT_WRITE;
        if (sec.Characteristics & formats::kScnMemExecute) flags |= PROT_EXEC | PROT_READ;
        mark(sec.VirtualAddress, align_up(formats::virtual_size(sec), opt_.SectionAlignment), flags);
    }

    for (size_t start = 0; start < prot.size();) {
        size_t end = start;
        while (end < prot.size() && prot[end] == prot[start]) end++;
        if (mprotect(base_ + start * page, (end - start) * page, prot[start]) != 0) {
            throw LoadError("メモリ保護の設定に失敗しました");
        }
        start = end;
    }
}

std::string MappedImage::read_cstring(uint64_t rva) const {
    auto p = reinterpret_cast<const char*>(checked(rva, 1));
    size_t len = strnlen(p, size_ - rva);
    if (len == size_ - rva) {
        throw LoadError("文字列がイメージ範囲内で終端していません");
    }
    return std::string(p, len);
}

std::optional<uint32_t> MappedImage::export_index_by_name(const formats::ExportDirectory& dir,
                                                         const ImportSymbol& sym) const {
    auto name_at = [&](uint32_t i) {
        return read_cstring(read<uint32_t>(static_cast<uint64_t>(dir.AddressOfNames) + i * sizeof(uint32_t)));
    };
    auto ordinal_at = [&](uint32_t i) {
        return read<uint16_t>(static_cast<uint64_t>(dir.AddressOfNameOrdinals) + i * sizeof(uint16_t));
    };

    // まずヒントの位置を見て，外れたら名前テーブル (昇順) を二分探索する
    if (sym.hint < dir.NumberOfNames && name_at(sym.hint) == sym.name) {
        return ordinal_at(sym.hint);
    }

    uint32_t lo = 0, hi = dir.NumberOfNames;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int cmp = name_at(mid).compare(sym.name);
        if (cmp == 0) return ordinal_at(mid);
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    return std::nullopt;
}

std::optional<ExportTarget> MappedImage::find_export(const ImportSymbol& sym) const {
    if (export_dir_.Size == 0) return std::nullopt;
    auto dir = read<formats::ExportDirectory>(export_dir_.VirtualAddress);

    std::optional<uint32_t> index;
    if (sym.ordinal) {
        if (*sym.ordinal >= dir.Base) index = *sym.ordinal - dir.Base;
    } else {
        index = export_index_by_name(dir, sym);
    }
    if (!index || *index >= dir.NumberOfFunctions) return std::nullopt;

    auto rva = read<uint32_t>(static_cast<uint64_t>(dir.AddressOfFunctions) + *index * sizeof(uint32_t));
    if (rva == 0) return std::nullopt;

    // エクスポートディレクトリの範囲内を指していればフォワーダ文字列
    ExportTarget target;
    if (rva >= export_dir_.VirtualAddress && rva - export_dir_.VirtualAddress < export_dir_.Size) {
        target.forwarder = read_cstring(rva);
    } else {
        target.address = checked(rva, 1);
    }
    return target;
}

std::optional<formats::TlsDirectory64> MappedImage::tls_directory() const {
    if (tls_dir_.Size == 0) return std::nullopt;
    return read<formats::TlsDirectory64>(tls_dir_.VirtualAddress);
}

bool MappedImage::is_code(const void* p) const {
    if (!contains(p)) return false;
    uint64_t rva = static_cast<const uint8_t*>(p) - base_;
    for (const auto& sec : sections_) {
        if (rva >= sec.VirtualAddress && rva - sec.VirtualAddress < formats::virtual_size(sec)) {
            return sec.Characteristics & formats::kScnMemExecute;
        }
    }
    return false;
}

}
