# PE 形式の DLL を mingw-w64 でビルドするための関数
#
# 事前に以下を設定しておくこと:
#   MINGW_CC          x86_64-w64-mingw32-gcc
#   MINGW_DLLTOOL     x86_64-w64-mingw32-dlltool
#   NTMAC_DLL_DIR     DLL の出力先 (ローダの既定の探索パス)
#   NTMAC_IMPLIB_DIR  import ライブラリの出力先
#   NTMAC_PE_CFLAGS   共通のコンパイルオプション
#   NTMAC_PE_HEADERS  依存するヘッダ (変更時に再ビルドするため)

# import ライブラリは，それを生成するターゲット (<dll>_implib) を1つだけ持つ．
# Makefile ジェネレータで生成規則が複数のターゲットに複製されて並列ビルドで衝突しないよう，
# 依存する側はファイルではなくこのターゲットに依存する (CMP0113)．

# ntmac_add_pe_dll(<name>
#     SOURCES <file>...     DLL のソース (src/dlls/<name>/ からの相対パス)
#     DEF <file>            エクスポートの定義
#     [DEFINES <def>...]
#     [IMPORTS <dll>...])   インポートする DLL の名前 (ntdll, ntmachost など)
#
# <name>.dll と lib<name>.a を生成し，ターゲット <name>_dll と <name>_implib を作る．
function(ntmac_add_pe_dll name)
    cmake_parse_arguments(ARG "" "DEF" "SOURCES;DEFINES;IMPORTS" ${ARGN})

    set(dir ${CMAKE_SOURCE_DIR}/src/dlls/${name})
    list(TRANSFORM ARG_SOURCES PREPEND ${dir}/ OUTPUT_VARIABLE sources)
    list(TRANSFORM ARG_DEFINES PREPEND -D OUTPUT_VARIABLE defines)
    set(dll ${NTMAC_DLL_DIR}/${name}.dll)
    set(implib ${NTMAC_IMPLIB_DIR}/lib${name}.a)
    list(TRANSFORM ARG_IMPORTS REPLACE "(.+)" "${NTMAC_IMPLIB_DIR}/lib\\1.a" OUTPUT_VARIABLE libs)

    add_custom_command(
        OUTPUT ${dll} ${implib}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NTMAC_DLL_DIR} ${NTMAC_IMPLIB_DIR}
        COMMAND ${MINGW_CC} ${NTMAC_PE_CFLAGS} ${defines} -shared -e DllMain
                ${sources} ${dir}/${ARG_DEF} ${libs}
                -Wl,--out-implib,${implib} -o ${dll}
        DEPENDS ${sources} ${dir}/${ARG_DEF} ${libs} ${NTMAC_PE_HEADERS}
        COMMENT "Building PE DLL ${name}.dll"
        VERBATIM)
    add_custom_target(${name}_implib DEPENDS ${implib})
    add_custom_target(${name}_dll ALL DEPENDS ${dll})
    add_dependencies(${name}_implib ${name}_dll)

    foreach(import IN LISTS ARG_IMPORTS)
        add_dependencies(${name}_dll ${import}_implib)
    endforeach()
endfunction()

# ntmac_add_implib(<name> DEF <file>)
#
# ホストに組み込まれた DLL (NTMACHOST.DLL) のように，実体が PE でない DLL の import ライブラリだけを作る．
function(ntmac_add_implib name)
    cmake_parse_arguments(ARG "" "DEF" "" ${ARGN})
    set(implib ${NTMAC_IMPLIB_DIR}/lib${name}.a)

    # dlltool は作業ディレクトリに一時ファイルを作るので，出力先で実行する
    add_custom_command(
        OUTPUT ${implib}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NTMAC_IMPLIB_DIR}
        COMMAND ${MINGW_DLLTOOL} -d ${ARG_DEF} -l ${implib}
        DEPENDS ${ARG_DEF}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating import library lib${name}.a"
        VERBATIM)
    add_custom_target(${name}_implib DEPENDS ${implib})
endfunction()
