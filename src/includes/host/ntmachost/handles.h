#pragma once
#include <cstdint>
#include <optional>

namespace ntmac::ntmachost {

/**
 * NT のハンドル値とホストのファイルディスクリプタの対応表．
 * ハンドル値は NT と同様に4の倍数を使う．標準入力・標準出力・標準エラー出力は起動時に登録済み．
 */
uint64_t std_handle(int fd);
std::optional<int> fd_from_handle(uint64_t handle);

}
