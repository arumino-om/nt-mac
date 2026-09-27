/*
 * gs ベースの切り替えを伴う，PE コードとホストコードの境界．
 *
 * PE コードの実行中は gs ベースが TEB を指し，ホストコードの実行中は macOS の TSD を指す．
 * 切り替えの前後でコンパイラ生成コードが gs 経由の TSD アクセスをしないよう，境界はすべてここに書く．
 */
#include "shared/teb.h"

/* machdep syscall: thread_fast_set_cthread_self64 (非公開)．rdi = 新しい gs ベース */
#define SYS_SET_GS_BASE 0x3000003

/* PE→ホストの呼び出しでコピーするスタック渡しの引数 (第5引数以降) の数 */
#define HOST_STACK_ARGS 12

    .text

/*
 * uint64_t ntmac_call_pe(ntmac_teb* teb, void* fn, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
 *
 * ホスト→PE (SysV で呼ばれる)．gs を teb に切り替えて fn(a1, a2, a3, a4) を ms_abi で呼び，
 * 戻ったら gs を teb->HostTsd に戻す．
 */
    .globl _ntmac_call_pe
    .p2align 4
_ntmac_call_pe:
    push    %rbp
    mov     %rsp, %rbp
    push    %rbx
    push    %r12
    sub     $32, %rsp                   /* ms_abi のシャドウ領域 (兼 引数の退避場所) */
    mov     %rdi, %rbx                  /* rbx = TEB */
    mov     %rsi, %r12                  /* r12 = fn */
    mov     %rdx, 0(%rsp)               /* syscall が rcx/r11 などを壊すので退避 */
    mov     %rcx, 8(%rsp)
    mov     %r8, 16(%rsp)
    mov     %r9, 24(%rsp)

    mov     %rbx, %rdi
    mov     $SYS_SET_GS_BASE, %eax
    syscall                             /* gs = TEB */

    mov     0(%rsp), %rcx
    mov     8(%rsp), %rdx
    mov     16(%rsp), %r8
    mov     24(%rsp), %r9
    call    *%r12

    mov     %rax, %r12
    mov     NTMAC_TEB_HOST_TSD(%rbx), %rdi
    mov     $SYS_SET_GS_BASE, %eax
    syscall                             /* gs = ホスト */
    mov     %r12, %rax

    add     $32, %rsp
    pop     %r12
    pop     %rbx
    pop     %rbp
    ret

/*
 * PE→ホストの汎用 trampoline．
 *
 * 組み込み関数ごとのスタブが rax = 呼び出す ms_abi のホスト関数 をセットしてここへ jmp する．
 * それ以外のレジスタとスタックは PE 側からの ms_abi の呼び出しそのままの状態．
 * gs をホストに切り替え，引数を積み直してホスト関数を呼び，gs を TEB に戻して return する．
 */
    .globl _ntmac_host_trampoline
    .p2align 4
_ntmac_host_trampoline:
    push    %rbp
    mov     %rsp, %rbp
    push    %rbx                        /* rbx/rsi/rdi は ms_abi の callee-saved */
    push    %rsi
    push    %rdi
    sub     $(32 + 8 * HOST_STACK_ARGS + 24), %rsp  /* シャドウ + スタック引数 + 戻り値の退避 (16バイト境界を維持) */
    mov     %rax, %rsi                  /* rsi = ホスト関数 */
    mov     %gs:0x30, %rbx              /* rbx = TEB */

    mov     %rcx, 0(%rsp)
    mov     %rdx, 8(%rsp)
    mov     %r8, 16(%rsp)
    mov     %r9, 24(%rsp)

    /* 呼び出し元のスタック引数は [rbp + 16 (戻り先と旧rbp) + 32 (シャドウ)] から */
    xor     %ecx, %ecx
1:  mov     48(%rbp, %rcx, 8), %rax
    mov     %rax, 32(%rsp, %rcx, 8)
    inc     %ecx
    cmp     $HOST_STACK_ARGS, %ecx
    jne     1b

    mov     NTMAC_TEB_HOST_TSD(%rbx), %rdi
    mov     $SYS_SET_GS_BASE, %eax
    syscall                             /* gs = ホスト */

    mov     0(%rsp), %rcx
    mov     8(%rsp), %rdx
    mov     16(%rsp), %r8
    mov     24(%rsp), %r9
    call    *%rsi

    mov     %rax, %rsi
    movdqu  %xmm0, (32 + 8 * HOST_STACK_ARGS)(%rsp)  /* 浮動小数点の戻り値 */
    mov     %rbx, %rdi
    mov     $SYS_SET_GS_BASE, %eax
    syscall                             /* gs = TEB */
    movdqu  (32 + 8 * HOST_STACK_ARGS)(%rsp), %xmm0
    mov     %rsi, %rax

    lea     -24(%rbp), %rsp
    pop     %rdi
    pop     %rsi
    pop     %rbx
    pop     %rbp
    ret

/*
 * PE→PE の呼び出しを記録するリレー (NTMAC_TRACE=relay)．PE モードで動く．
 *
 * make_jump_entry() が生成する入口から rax = relay_info* で jmp してくる．
 * relay_info は { const char* name; void* target; }．
 * 引数を保存して ntmac_relay_enter(info, 引数の配列) を，本来の関数 target を呼んだ後に
 * ntmac_relay_leave(info, 戻り値) を呼ぶ (どちらも ms_abi のホスト関数で，gs の切り替えは trampoline が行う)．
 */
#define RELAY_XMM_SAVE (32 + 8 * HOST_STACK_ARGS)   /* xmm0-3 の退避場所 (64バイト) */
#define RELAY_FRAME    (RELAY_XMM_SAVE + 64)

    .globl _ntmac_relay_thunk
    .p2align 4
_ntmac_relay_thunk:
    push    %rbp
    mov     %rsp, %rbp
    push    %rbx
    push    %r12
    sub     $RELAY_FRAME, %rsp
    mov     %rax, %rbx                  /* rbx = relay_info */

    /* レジスタ引数は呼び出し元が用意したシャドウ領域に置く．[rbp+16] から引数が順に並ぶ */
    mov     %rcx, 16(%rbp)
    mov     %rdx, 24(%rbp)
    mov     %r8, 32(%rbp)
    mov     %r9, 40(%rbp)
    movdqu  %xmm0, (RELAY_XMM_SAVE + 0)(%rsp)
    movdqu  %xmm1, (RELAY_XMM_SAVE + 16)(%rsp)
    movdqu  %xmm2, (RELAY_XMM_SAVE + 32)(%rsp)
    movdqu  %xmm3, (RELAY_XMM_SAVE + 48)(%rsp)

    mov     %rbx, %rcx
    lea     16(%rbp), %rdx
    lea     _ntmac_relay_enter(%rip), %rax
    call    _ntmac_host_trampoline

    /* スタック引数 (第5引数以降) を積み直す */
    xor     %ecx, %ecx
1:  mov     48(%rbp, %rcx, 8), %rax
    mov     %rax, 32(%rsp, %rcx, 8)
    inc     %ecx
    cmp     $HOST_STACK_ARGS, %ecx
    jne     1b

    mov     16(%rbp), %rcx
    mov     24(%rbp), %rdx
    mov     32(%rbp), %r8
    mov     40(%rbp), %r9
    movdqu  (RELAY_XMM_SAVE + 0)(%rsp), %xmm0
    movdqu  (RELAY_XMM_SAVE + 16)(%rsp), %xmm1
    movdqu  (RELAY_XMM_SAVE + 32)(%rsp), %xmm2
    movdqu  (RELAY_XMM_SAVE + 48)(%rsp), %xmm3
    call    *8(%rbx)

    mov     %rax, %r12
    movdqu  %xmm0, (RELAY_XMM_SAVE + 0)(%rsp)
    mov     %rbx, %rcx
    mov     %rax, %rdx
    lea     _ntmac_relay_leave(%rip), %rax
    call    _ntmac_host_trampoline
    movdqu  (RELAY_XMM_SAVE + 0)(%rsp), %xmm0
    mov     %r12, %rax

    lea     -16(%rbp), %rsp
    pop     %r12
    pop     %rbx
    pop     %rbp
    ret

/*
 * void* ntmac_read_gs_self(void)
 *
 * gs:[0x30] を返す．gs が TEB を指していれば TEB 自身のアドレス．
 * 引数が無いので SysV からも ms_abi からも呼べる (起動時の自己診断とシグナルハンドラで使う)．
 */
    .globl _ntmac_read_gs_self
    .p2align 4
_ntmac_read_gs_self:
    mov     %gs:0x30, %rax
    ret

/*
 * void ntmac_set_gs_base(void* base)
 */
    .globl _ntmac_set_gs_base
    .p2align 4
_ntmac_set_gs_base:
    mov     $SYS_SET_GS_BASE, %eax
    syscall
    ret
