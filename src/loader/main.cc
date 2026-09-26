#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <vector>
#include "loader/readutils.h"
#include "loader/pe_image.h"
#include "loader/mapped_image.h"
#include "loader/exceptions/pe_format_error.h"
#include "loader/exceptions/load_error.h"
#include "nt/builtins.h"


int main(int argc, char** argv) {
    bool dump = false;
    const char* path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--dump") == 0) {
            dump = true;
        } else if (!path) {
            path = argv[i];
        }
    }

    if (!path) {
        std::fprintf(stderr, "usage: %s [--dump] <file.exe>\n", argv[0]);
        return 1;
    }

    std::vector<uint8_t> image;
    if (!ntmac::loader::read_file(path, image)) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }

    using namespace ntmac;
    try {
        std::optional<loader::MappedImage> mapped;
        {
            loader::PeImage pe(std::move(image));
            if (dump) {
                std::printf("size: %zu bytes\n", pe.data().size());
                pe.print_headers();
                pe.print_sections();
                return 0;
            }

            if (pe.file_header().Characteristics & formats::kFileDll) {
                std::cerr << "DLLは直接実行できません\n";
                return 1;
            }

            // 配置が済めばファイルの内容は不要なので，ここでPeImageを破棄する
            mapped.emplace(pe, nt::resolve_builtin);
        }
        return static_cast<int>(mapped->run());
    } catch (const loader::PeFormatError& e) {
        std::cerr << "PE format error: " << e.what() << '\n';
        return 1;
    } catch (const loader::LoadError& e) {
        std::cerr << "load error: " << e.what() << '\n';
        return 1;
    }
}
