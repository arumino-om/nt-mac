#pragma once
#include <cstdint>
#include "shared/ntstatus.h"

namespace ntmac::win32k {

/**
 * NTMACHOST のウィンドウ・描画のサービス (呼び出し番号 0x100 以降) を処理します．
 */
ntmac_ntstatus dispatch(uint32_t code, void* args);

}
