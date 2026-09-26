#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include "host/runtime/signals.h"
#include "host/runtime/process_env.h"
#include "host/runtime/thunks.h"

namespace ntmac::runtime {

namespace {

const loader::ModuleManager* g_modules = nullptr;

const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
    default:      return "signal";
    }
}

void on_crash(int sig, siginfo_t* info, void* context) {
    // gs が TEB を指している間は libc を呼べないので，最初に gs をホストに戻す
    auto self = ntmac_read_gs_self();
    bool in_pe = is_known_teb(self);
    if (in_pe) ntmac_set_gs_base(static_cast<ntmac_teb*>(self)->HostTsd);

    auto uc = static_cast<ucontext_t*>(context);
    auto rip = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rip);

    char where[512] = "";
    if (auto m = g_modules ? g_modules->find_by_address(rip) : nullptr) {
        auto offset = static_cast<uint8_t*>(rip) - m->image->base();
        std::snprintf(where, sizeof(where), " (%s+0x%tx)", m->name.c_str(), offset);
    }

    char msg[1024];
    int n = std::snprintf(msg, sizeof(msg), "\nfatal: %s in %s code at rip=%p%s, address=%p\n",
                          signal_name(sig), in_pe ? "PE" : "host", rip, where, info->si_addr);
    if (n > 0) write(STDERR_FILENO, msg, static_cast<size_t>(std::min<int>(n, sizeof(msg) - 1)));

    // 既定の動作に戻して return すると，同じ命令で再びシグナルが発生して異常終了する
    signal(sig, SIG_DFL);
}

}

void install_crash_handler(const loader::ModuleManager* modules) {
    g_modules = modules;

    struct sigaction sa {};
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) {
        sigaction(sig, &sa, nullptr);
    }
}

}
