#pragma once
#include <cstddef>
#include <cstdint>

namespace ntmac::formats {

constexpr uint16_t kDosMagic = 0x5A4D;        // "MZ"
constexpr uint32_t kNtSignature = 0x00004550; // "PE\0\0"
constexpr uint16_t kMachineAmd64 = 0x8664;
constexpr uint16_t kOptionalMagicPe32Plus = 0x20B;
constexpr size_t kNumDataDirectories = 16;

// FileHeader.Characteristics
constexpr uint16_t kFileRelocsStripped = 0x0001;
constexpr uint16_t kFileDll            = 0x2000;

// DataDirectoryのインデックス
constexpr size_t kDirExport    = 0;
constexpr size_t kDirImport    = 1;
constexpr size_t kDirBaseReloc = 5;
constexpr size_t kDirTls       = 9;
constexpr size_t kDirIat       = 12;

// ベースリロケーションの種類
constexpr uint16_t kRelBasedAbsolute = 0;
constexpr uint16_t kRelBasedHighLow  = 3;
constexpr uint16_t kRelBasedDir64    = 10;

// インポートルックアップテーブルの序数フラグ (PE32+)
constexpr uint64_t kOrdinalFlag64 = 0x8000000000000000ull;

constexpr uint32_t kScnCntCode       = 0x00000020;
constexpr uint32_t kScnCntInitData   = 0x00000040;
constexpr uint32_t kScnCntUninitData = 0x00000080;
constexpr uint32_t kScnMemDiscardable = 0x02000000;
constexpr uint32_t kScnMemExecute    = 0x20000000;
constexpr uint32_t kScnMemRead       = 0x40000000;
constexpr uint32_t kScnMemWrite      = 0x80000000;

struct SectionHeader {
    uint8_t Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

/**
 * メモリ上でのセクションサイズ．VirtualSizeが0の場合はSizeOfRawDataを使う．
 */
constexpr uint32_t virtual_size(const SectionHeader& s) {
    return s.VirtualSize != 0 ? s.VirtualSize : s.SizeOfRawData;
}

struct DosHeader {
    uint16_t e_magic;
    uint16_t _reserved[29];
    int32_t e_lfanew;
};

struct FileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct DataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct OptionalHeader64 {
    uint16_t Magic;
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    formats::DataDirectory DataDirectory[kNumDataDirectories];
};

struct ImportDescriptor {
    uint32_t OriginalFirstThunk; // Import Lookup Table のRVA
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;               // DLL名のRVA
    uint32_t FirstThunk;         // Import Address Table のRVA
};

struct BaseRelocationBlock {
    uint32_t VirtualAddress;
    uint32_t SizeOfBlock;        // このヘッダを含むブロック全体のサイズ
};

// 下記は検証用
static_assert(sizeof(DosHeader) == 64);
static_assert(offsetof(DosHeader, e_lfanew) == 0x3C);
static_assert(sizeof(FileHeader) == 20);
static_assert(offsetof(FileHeader, SizeOfOptionalHeader) == 16);
static_assert(sizeof(OptionalHeader64) == 240);
static_assert(offsetof(OptionalHeader64, ImageBase) == 24);
static_assert(offsetof(OptionalHeader64, SizeOfImage) == 56);
static_assert(offsetof(OptionalHeader64, DataDirectory) == 112);

static_assert(sizeof(ImportDescriptor) == 20);
static_assert(sizeof(BaseRelocationBlock) == 8);

static_assert(sizeof(SectionHeader) == 40);
static_assert(offsetof(SectionHeader, VirtualAddress) == 12);
static_assert(offsetof(SectionHeader, PointerToRawData) == 20);
static_assert(offsetof(SectionHeader, Characteristics) == 36);
}