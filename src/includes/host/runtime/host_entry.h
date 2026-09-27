#pragma once
#include <span>
#include <vector>

// PE コードから呼ばれる入口のコードを実行時に生成する．生成したコードは解放されない．

namespace ntmac::runtime {

/**
 * ms_abi のホスト関数 impl を，PE コードから直接呼べるようにする入口を生成します．
 * 入口は gs をホストに切り替えてから impl を呼び，戻る前に gs を TEB に戻します．
 */
void* make_host_entry(void* impl);
std::vector<void*> make_host_entries(std::span<void* const> impls);

/**
 * PE コードから呼ばれると，元の引数の代わりに impl(ctx, 呼び出し元の戻り先アドレス) を呼ぶ入口を生成します．
 * 未実装の関数のスタブに使います．impl は ms_abi のホスト関数．
 */
void* make_bound_host_entry(void* impl, const void* ctx);

/**
 * rax に ctx を入れて target (PE モードで動くアセンブリ) へ jmp する入口を生成します．
 * 引数のレジスタとスタックはそのまま渡ります．
 */
void* make_jump_entry(const void* target, const void* ctx);

}
