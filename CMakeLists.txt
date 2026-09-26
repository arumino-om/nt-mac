cmake_minimum_required(VERSION 3.20)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
project(pe_loader CXX ASM)

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
    src/host/runtime/process_env.cc
    src/host/runtime/signals.cc
    src/host/runtime/thunks.S
    src/host/ntmachost/builtins.cc
    src/host/ntmachost/handles.cc
    src/host/ntmachost/io.cc
    src/host/ntmachost/ntmachost.cc
)
target_include_directories(loader PRIVATE src/includes)
target_compile_options(loader PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
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
    -I${CMAKE_SOURCE_DIR}/src/includes -I${CMAKE_SOURCE_DIR}/src/dlls)
file(GLOB_RECURSE NTMAC_PE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/dlls/*.h ${CMAKE_SOURCE_DIR}/src/includes/shared/*.h)

# NTMACHOST.DLL はローダに組み込まれているので，import ライブラリだけを作る
ntmac_add_implib(ntmachost DEF ${CMAKE_SOURCE_DIR}/src/includes/shared/ntmachost.def)

ntmac_add_pe_dll(ntdll
    SOURCES main.c error.c file.c process.c
    DEF ntdll.def
    DEFINES NTMAC_BUILDING_NTDLL
    IMPORTS ntmachost)

ntmac_add_pe_dll(kernel32
    SOURCES main.c file.c process.c
    DEF kernel32.def
    IMPORTS ntdll)

# ---------------------------------------------------------------------------
# テスト
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME simple_hello COMMAND loader ${CMAKE_SOURCE_DIR}/src/tests/simple/hello.exe)
set_tests_properties(simple_hello PROPERTIES PASS_REGULAR_EXPRESSION "^Hello\n$")

set(DLL_TEST_DIR ${CMAKE_BINARY_DIR}/tests/dll)
set(DLL_TEST_SRC ${CMAKE_SOURCE_DIR}/src/tests/dll)
set(TEST_PE_FLAGS -O1 -nostdlib -Wall -Wextra)

# ImageBase を exe と衝突させ，必ず再配置されるようにする
add_custom_command(
    OUTPUT ${DLL_TEST_DIR}/testdll.dll ${DLL_TEST_DIR}/libtestdll.a
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DLL_TEST_DIR}
    COMMAND ${MINGW_CC} ${TEST_PE_FLAGS} -shared -e DllMain
            ${DLL_TEST_SRC}/testdll.c ${DLL_TEST_SRC}/testdll.def
            -Wl,--image-base,0x140000000 -Wl,--out-implib,${DLL_TEST_DIR}/libtestdll.a
            -o ${DLL_TEST_DIR}/testdll.dll
    DEPENDS ${DLL_TEST_SRC}/testdll.c ${DLL_TEST_SRC}/testdll.def
    VERBATIM)
add_custom_command(
    OUTPUT ${DLL_TEST_DIR}/dlltest.exe
    COMMAND ${MINGW_CC} ${TEST_PE_FLAGS} -e start ${DLL_TEST_SRC}/main.c
            ${DLL_TEST_DIR}/libtestdll.a -lkernel32 -o ${DLL_TEST_DIR}/dlltest.exe
    DEPENDS ${DLL_TEST_SRC}/main.c ${DLL_TEST_DIR}/libtestdll.a
    VERBATIM)
add_custom_target(pe_tests ALL DEPENDS ${DLL_TEST_DIR}/testdll.dll ${DLL_TEST_DIR}/dlltest.exe)

add_test(NAME dll_loading COMMAND loader ${DLL_TEST_DIR}/dlltest.exe)
set_tests_properties(dll_loading PROPERTIES PASS_REGULAR_EXPRESSION "^dll test ok\n$")
