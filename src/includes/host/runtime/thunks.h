#pragma once
#include <cstdint>
#include "shared/teb.h"

// src/host/thunks.S で定義
extern "C" {

/**
 * gs を teb に切り替えて，ms_abi の関数 fn(a1, a2, a3, a4) を呼びます．戻ると gs はホストに戻ります．
 */
uint64_t ntmac_call_pe(ntmac_teb* teb, const void* fn,
                       uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0);

/**
 * PE→ホストの汎用 trampoline．直接は呼ばず，make_host_entries() が生成するスタブから jmp します．
 */
void ntmac_host_trampoline();

/**
 * gs:[0x30] を返します．
 */
void* ntmac_read_gs_self();

void ntmac_set_gs_base(void* base);

}
