#pragma once
#include <cstddef>
#include <functional>

namespace ntmac::runtime {

/**
 * PE コードを実行するためのスレッドを作ります．スタックは stack_size バイト (Windows の SizeOfStackReserve 相当)．
 * body の中で TEB を作り，PE のコードを呼ぶこと．
 */
void start_pe_thread(size_t stack_size, std::function<void()> body);

}
