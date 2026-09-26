#pragma once
#include <span>
#include <vector>

namespace ntmac::runtime {

/**
 * ms_abi のホスト関数それぞれについて，PE コードから直接呼べる入口スタブを生成します．
 * スタブは gs をホストに切り替えてから関数を呼び，戻る前に gs を TEB に戻します．
 * 戻り値は impls と同じ順序のスタブのアドレスです．生成したスタブは解放されません．
 */
std::vector<void*> make_host_entries(std::span<void* const> impls);

}
