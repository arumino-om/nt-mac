#pragma once
#include <string>
#include <vector>
#include "shared/teb.h"

namespace ntmac::runtime {

struct PebParams {
    void* image_base;
    uint32_t subsystem;
    uint32_t subsystem_major;
    uint32_t subsystem_minor;
    std::string image_path;             // UTF-8
    std::vector<std::string> args;      // argv[0] を含む (UTF-8)
};

/**
 * gs ベースを TEB に切り替えられることを確認します．失敗した場合は理由を返します．
 */
std::string check_gs_switch();

/**
 * プロセスの PEB を作成します．
 */
ntmac_peb* create_peb(const PebParams& params);

/**
 * 呼び出したスレッドの TEB を作成し，このスレッドの現在の TEB として登録します．
 */
ntmac_teb* create_teb(ntmac_peb* peb);

/**
 * 現在のスレッドの TEB．ホストコード (gs がホストを指している状態) からのみ呼べます．
 */
ntmac_teb* current_teb();

/**
 * p が create_teb() で作成した TEB かどうか．gs に依存しないので，シグナルハンドラからも呼べます．
 */
bool is_known_teb(const void* p);

}
