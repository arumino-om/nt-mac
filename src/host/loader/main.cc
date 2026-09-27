#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "host/runtime/pe_thread.h"
#include "host/runtime/process_env.h"
#include "host/win32k/main_thread.h"
#include "host/runtime/signals.h"
#include "host/runtime/thunks.h"
#include "host/runtime/trace.h"
#include "host/runtime/unicode.h"
#include "host/loader/exceptions/load_error.h"
#include "host/loader/exceptions/pe_format_error.h"
#include "host/loader/module_manager.h"
#include "host/loader/pe_image.h"
#include "host/loader/readutils.h"

namespace fs = std::filesystem;
using namespace ntmac;

namespace {

int dump(const char* path) {
    std::vector<uint8_t> image;
    if (!loader::read_file(path, image)) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }

    loader::PeImage pe(std::move(image));
    std::printf("size: %zu bytes\n", pe.data().size());
    pe.print_headers();
    pe.print_sections();
    return 0;
}

/**
 * DLL の探索パス: 実行ファイルと同じディレクトリ，環境変数 NTMAC_DLL_PATH (':' 区切り)，ビルド時の既定ディレクトリ
 */
std::vector<fs::path> dll_search_paths(const fs::path& exe) {
    std::vector<fs::path> paths{exe.parent_path()};

    if (auto env = std::getenv("NTMAC_DLL_PATH")) {
        std::string s(env);
        for (size_t pos = 0; pos <= s.size();) {
            auto next = s.find(':', pos);
            if (next == std::string::npos) next = s.size();
            if (next > pos) paths.emplace_back(s.substr(pos, next - pos));
            pos = next + 1;
        }
    }

#ifdef NTMAC_DEFAULT_DLL_DIR
    paths.emplace_back(NTMAC_DEFAULT_DLL_DIR);
#endif
    return paths;
}

int run(int argc, char** argv, int first_arg) {
    fs::path exe_path = fs::absolute(argv[first_arg]);

    if (auto err = runtime::check_gs_switch(); !err.empty()) {
        std::cerr << "この環境では PE コードを実行できません: " << err << '\n';
        return 1;
    }

    loader::ModuleManager modules(dll_search_paths(exe_path));
    runtime::init_trace(&modules);
    auto& exe = modules.load_executable(exe_path);

    const auto& opt = exe.image->optional_header();

    runtime::PebParams params{
        .image_base = exe.image->base(),
        .subsystem = opt.Subsystem,
        .subsystem_major = opt.MajorSubsystemVersion,
        .subsystem_minor = opt.MinorSubsystemVersion,
        .image_path = exe_path.string(),
        .args = {runtime::utf16_to_utf8(runtime::to_windows_path(exe_path))},
    };
    for (int i = first_arg + 1; i < argc; i++) params.args.emplace_back(argv[i]);

    auto peb = runtime::create_peb(params);
    runtime::install_crash_handler(&modules);

    auto entry = exe.image->entry_point();
    if (!entry) throw loader::LoadError("エントリポイントがありません");

    // PE のメインスレッド．プロセスのメインスレッドは Cocoa (AppKit) のために空けておく．
    // Windows の既定 (SizeOfStackReserve) より小さくはしない．ホストのコードも同じスタックを使うので最低 8MB にする
    size_t stack_size = std::max<uint64_t>(opt.SizeOfStackReserve, 8 * 1024 * 1024);
    runtime::start_pe_thread(stack_size, [&modules, peb, entry] {
        int code = 1;
        try {
            auto teb = runtime::create_teb(peb);
            modules.setup_thread_tls(teb);
            modules.initialize(teb);

            // プロセスのエントリポイントは PEB* を引数に呼ばれる．戻った場合はその値を終了コードにする
            code = static_cast<int>(ntmac_call_pe(teb, entry, reinterpret_cast<uint64_t>(peb)));
            modules.shutdown(teb); // エントリポイントから戻った場合も ExitProcess と同じ終了処理をする
        } catch (const std::runtime_error& e) {
            std::cerr << "load error: " << e.what() << '\n';
        }
        std::fflush(nullptr);
        std::exit(code);
    });

    win32k::run_main_thread();
}

}

int main(int argc, char** argv) {
    bool dump_only = false;
    int first_arg = 1;
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        dump_only = true;
        first_arg = 2;
    }

    if (first_arg >= argc) {
        std::fprintf(stderr, "usage: %s [--dump] <file.exe> [args...]\n", argv[0]);
        return 1;
    }

    try {
        return dump_only ? dump(argv[first_arg]) : run(argc, argv, first_arg);
    } catch (const loader::PeFormatError& e) {
        std::cerr << "PE format error: " << e.what() << '\n';
        return 1;
    } catch (const loader::LoadError& e) {
        std::cerr << "load error: " << e.what() << '\n';
        return 1;
    }
}
