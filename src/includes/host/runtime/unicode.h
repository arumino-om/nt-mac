#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace ntmac::runtime {

/**
 * UTF-8 を UTF-16 に変換します．不正なバイト列は U+FFFD に置き換えます．
 */
std::u16string utf8_to_utf16(std::string_view s);

/**
 * UTF-16 を UTF-8 に変換します．対になっていないサロゲートは U+FFFD に置き換えます．
 */
std::string utf16_to_utf8(std::u16string_view s);

/**
 * 終端付きの UTF-16 文字列 (PE 側のメモリ) を std::u16string_view にします．
 */
std::u16string_view utf16_view(const uint16_t* s);

/**
 * ホストのパスを Windows のパスにします．Wine と同じく Z:\ をホストのルート / に対応させる．
 */
std::u16string to_windows_path(const std::filesystem::path& path);

/**
 * Windows のパスをホストのパスにします．Z: 以外のドライブや UNC は未対応 (空のパスを返す)．
 * ドライブもルートも無い相対パスは，区切り文字だけを変換します．
 */
std::filesystem::path from_windows_path(std::u16string_view path);

}
