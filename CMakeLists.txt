cmake_minimum_required(VERSION 3.20)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
project(pe_loader CXX OBJCXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# PE の DLL の出力先 (ローダの既定の探索パス) と import ライブラリの出力先
set(NTMAC_DLL_DIR ${CMAKE_BINARY_DIR}/dlls)
set(NTMAC_IMPLIB_DIR ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# ホスト側: ローダ (clang)
# ---------------------------------------------------------------------------
add_executable(loader
    src/host/loader/main.cc
    src/host/loader/pe_image.cc
    src/host/loader/mapped_image.cc
    src/host/loader/module_manager.cc
    src/host/loader/readutils.cc
    src/host/runtime/host_entry.cc
    src/host/runtime/pe_thread.cc
    src/host/runtime/process_env.cc
    src/host/runtime/signals.cc
    src/host/runtime/thunks.S
    src/host/runtime/trace.cc
    src/host/runtime/unicode.cc
    src/host/ntmachost/builtins.cc
    src/host/ntmachost/handles.cc
    src/host/ntmachost/io.cc
    src/host/ntmachost/memory.cc
    src/host/ntmachost/module.cc
    src/host/ntmachost/ntmachost.cc
    src/host/ntmachost/process.cc
    src/host/ntmachost/sync.cc
    src/host/win32k/cocoa.mm
    src/host/win32k/gdi.cc
    src/host/win32k/main_thread.mm
    src/host/win32k/services.cc
    src/host/win32k/state.cc
)
target_include_directories(loader PRIVATE src/includes)
target_compile_options(loader PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall -Wextra>
                                      $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
set_target_properties(loader PROPERTIES OBJCXX_STANDARD 20)
target_link_libraries(loader PRIVATE "-framework AppKit" "-framework CoreText" "-framework QuartzCore" "-framework ImageIO"
                                     "-framework UniformTypeIdentifiers")
target_compile_definitions(loader PRIVATE NTMAC_DEFAULT_DLL_DIR="${NTMAC_DLL_DIR}")

# ---------------------------------------------------------------------------
# PE 側: DLL (mingw-w64)
# ---------------------------------------------------------------------------
find_program(MINGW_CC x86_64-w64-mingw32-gcc REQUIRED)
find_program(MINGW_DLLTOOL x86_64-w64-mingw32-dlltool REQUIRED)
include(cmake/PeDll.cmake)

# CRT を使わず単体で動くようにする．__chkstk なども使わない
set(NTMAC_PE_CFLAGS
    -O2 -Wall -Wextra -nostdlib -ffreestanding -fno-stack-protector -mno-stack-arg-probe
    -fno-tree-loop-distribute-patterns  # memset などの実装がそれ自身の呼び出しに最適化されないようにする
    -mcx16                              # SList で cmpxchg16b を使う
    -I${CMAKE_SOURCE_DIR}/src/includes -I${CMAKE_SOURCE_DIR}/src/dlls)
file(GLOB_RECURSE NTMAC_PE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/dlls/*.h ${CMAKE_SOURCE_DIR}/src/includes/shared/*.h)

# NTMACHOST.DLL はローダに組み込まれているので，import ライブラリだけを作る
ntmac_add_implib(ntmachost DEF ${CMAKE_SOURCE_DIR}/src/includes/shared/ntmachost.def)

ntmac_add_pe_dll(ntdll
    SOURCES main.c critsec.c error.c file.c heap.c loader.c misc.c process.c resource.c string.c sync.c time.c version.c virtual.c
    DEF ntdll.def
    DEFINES NTMAC_BUILDING_NTDLL
    IMPORTS ntmachost)

ntmac_add_pe_dll(kernel32
    SOURCES main.c codepage.c file.c memory.c module.c process.c resource.c thread.c time.c
    DEF kernel32.def
    IMPORTS ntdll)

ntmac_add_pe_dll(gdi32
    SOURCES main.c dc.c object.c
    DEF gdi32.def
    IMPORTS ntmachost ntdll)

ntmac_add_pe_dll(user32
    SOURCES main.c class.c controls.c dialog.c icon.c message.c paint.c rect.c resource.c sysparams.c text.c window.c
    DEF user32.def
    IMPORTS ntmachost ntdll kernel32 gdi32)

ntmac_add_pe_dll(ucrtbase
    SOURCES main.c args.c heap.c printf.c runtime.c stdio.c string.c
    DEF ucrtbase.def
    IMPORTS kernel32)

# ---------------------------------------------------------------------------
# テスト
# ---------------------------------------------------------------------------
enable_testing()
set(TEST_BIN_DIR ${CMAKE_BINARY_DIR}/tests)
set(TEST_SRC_DIR ${CMAKE_SOURCE_DIR}/src/tests)

# ntmac_add_pe_test_exe(<name> SOURCES <file>... [RC <file>] [FLAGS <flag>...] [LIBS <lib>...] [DEPENDS <file>...])
#   src/tests/ 以下のソースから ${TEST_BIN_DIR}/<name>.exe を mingw でビルドする．LIBS はソースの後ろに置かれる．
#   RC を指定するとリソースを windres でコンパイルして一緒にリンクする
find_program(MINGW_WINDRES x86_64-w64-mingw32-windres REQUIRED)
function(ntmac_add_pe_test_exe name)
    cmake_parse_arguments(ARG "" "RC" "SOURCES;FLAGS;LIBS;DEPENDS" ${ARGN})
    list(TRANSFORM ARG_SOURCES PREPEND ${TEST_SRC_DIR}/ OUTPUT_VARIABLE sources)
    set(res_commands "")
    set(res_objects "")
    if(ARG_RC)
        set(res ${TEST_BIN_DIR}/${name}.res.o)
        set(res_commands COMMAND ${MINGW_WINDRES} ${TEST_SRC_DIR}/${ARG_RC} -O coff -o ${res})
        set(res_objects ${res})
        list(APPEND ARG_DEPENDS ${TEST_SRC_DIR}/${ARG_RC})
    endif()
    add_custom_command(
        OUTPUT ${TEST_BIN_DIR}/${name}.exe
        COMMAND ${CMAKE_COMMAND} -E make_directory ${TEST_BIN_DIR}
        ${res_commands}
        COMMAND ${MINGW_CC} -Wall -Wextra ${ARG_FLAGS} ${sources} ${res_objects} ${ARG_LIBS} -o ${TEST_BIN_DIR}/${name}.exe
        DEPENDS ${sources} ${ARG_DEPENDS}
        VERBATIM)
    add_custom_target(test_${name} ALL DEPENDS ${TEST_BIN_DIR}/${name}.exe)
endfunction()

# ntmac_add_test(<name> <exe> [args...])
function(ntmac_add_test name exe)
    add_test(NAME ${name} COMMAND loader ${exe} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 10)
endfunction()

# CRT を使わない最小のプログラム
ntmac_add_pe_test_exe(simple_hello SOURCES simple/main.c FLAGS -O1 -nostdlib -e start LIBS -lkernel32)
ntmac_add_test(simple_hello ${TEST_BIN_DIR}/simple_hello.exe)
set_tests_properties(simple_hello PROPERTIES PASS_REGULAR_EXPRESSION "^Hello\n$")

# DLL の読み込み: 再配置・DllMain・名前/序数/フォワーダによるインポート・TEB/PEB
# testdll.dll は ImageBase を exe と衝突させ，必ず再配置されるようにする
add_custom_command(
    OUTPUT ${TEST_BIN_DIR}/testdll.dll ${TEST_BIN_DIR}/libtestdll.a
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TEST_BIN_DIR}
    COMMAND ${MINGW_CC} -O1 -nostdlib -Wall -Wextra -shared -e DllMain
            ${TEST_SRC_DIR}/dll/testdll.c ${TEST_SRC_DIR}/dll/testdll.def
            -Wl,--image-base,0x140000000 -Wl,--out-implib,${TEST_BIN_DIR}/libtestdll.a
            -o ${TEST_BIN_DIR}/testdll.dll
    DEPENDS ${TEST_SRC_DIR}/dll/testdll.c ${TEST_SRC_DIR}/dll/testdll.def
    VERBATIM)
add_custom_target(test_testdll ALL DEPENDS ${TEST_BIN_DIR}/testdll.dll)
ntmac_add_pe_test_exe(dlltest
    SOURCES dll/main.c
    FLAGS -O1 -nostdlib -e start
    LIBS ${TEST_BIN_DIR}/libtestdll.a -lkernel32
    DEPENDS ${TEST_BIN_DIR}/libtestdll.a)
ntmac_add_test(dll_loading ${TEST_BIN_DIR}/dlltest.exe)
set_tests_properties(dll_loading PROPERTIES PASS_REGULAR_EXPRESSION "^dll test ok\n$")

# CRT (UCRT) を使うプログラム．-O0 では printf，-O2 では puts が使われる
ntmac_add_pe_test_exe(crt_hello_printf SOURCES crt/hello.c FLAGS -O0)
ntmac_add_pe_test_exe(crt_hello_puts SOURCES crt/hello.c FLAGS -O2)
ntmac_add_pe_test_exe(crt_features SOURCES crt/features.c FLAGS -O2 -Wno-format-truncation)

ntmac_add_test(crt_hello_printf ${TEST_BIN_DIR}/crt_hello_printf.exe)
ntmac_add_test(crt_hello_puts ${TEST_BIN_DIR}/crt_hello_puts.exe)
set_tests_properties(crt_hello_printf crt_hello_puts PROPERTIES PASS_REGULAR_EXPRESSION "^Hello World\n$")

ntmac_add_test(crt_features ${TEST_BIN_DIR}/crt_features.exe one "two words" "three\"quote")
set_tests_properties(crt_features PROPERTIES
    ENVIRONMENT NTMAC_TEST_ENV=hello
    PASS_REGULAR_EXPRESSION "^crt test ok\natexit called\n$")

# ウィンドウの基本動作 (画面は使わない)
ntmac_add_pe_test_exe(gui_window SOURCES gui/window.c FLAGS -O1 -municode -mwindows)
ntmac_add_test(gui_window ${TEST_BIN_DIR}/gui_window.exe)
set_tests_properties(gui_window PROPERTIES
    ENVIRONMENT NTMAC_NO_DISPLAY=1
    PASS_REGULAR_EXPRESSION "^gui test ok\n$")

# ダイアログと標準コントロール
ntmac_add_pe_test_exe(gui_dialog SOURCES gui/dialog.c RC gui/dialog.rc FLAGS -O1 -municode -mwindows)
ntmac_add_test(gui_dialog ${TEST_BIN_DIR}/gui_dialog.exe)
set_tests_properties(gui_dialog PROPERTIES
    ENVIRONMENT NTMAC_NO_DISPLAY=1
    PASS_REGULAR_EXPRESSION "^dialog test ok\n$")

# MSVC でビルドしたプログラム (リポジトリには含めない)．.temp/ に exe と native の DLL がある場合のみ
set(MSVC_SAMPLE_DIR ${CMAKE_SOURCE_DIR}/.temp)
if(EXISTS ${MSVC_SAMPLE_DIR}/ConsoleApplication1.exe AND EXISTS ${MSVC_SAMPLE_DIR}/msvcp140.dll)
    ntmac_add_test(msvc_console_hello ${MSVC_SAMPLE_DIR}/ConsoleApplication1.exe)
    set_tests_properties(msvc_console_hello PROPERTIES PASS_REGULAR_EXPRESSION "^Hello World!\r?\n$")
else()
    message(STATUS ".temp/ に MSVC のサンプルが無いため，msvc_console_hello のテストは追加しません")
endif()
