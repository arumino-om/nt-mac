cmake_minimum_required(VERSION 3.20)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
project(pe_loader CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(loader
    src/loader/main.cc
    src/loader/pe_image.cc
    src/loader/mapped_image.cc
    src/loader/readutils.cc
    src/nt/builtins.cc
    src/nt/kernel32.cc
)
target_include_directories(loader PRIVATE src/includes)
target_compile_options(loader PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME simple_hello COMMAND loader ${CMAKE_SOURCE_DIR}/src/tests/simple/hello.exe)
set_tests_properties(simple_hello PROPERTIES PASS_REGULAR_EXPRESSION "^Hello\n$")
