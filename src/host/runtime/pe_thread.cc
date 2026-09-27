#include <pthread.h>
#include <memory>
#include "host/loader/exceptions/load_error.h"
#include "host/runtime/pe_thread.h"

namespace ntmac::runtime {

namespace {

void* thread_main(void* arg) {
    std::unique_ptr<std::function<void()>> body(static_cast<std::function<void()>*>(arg));
    (*body)();
    return nullptr;
}

}

void start_pe_thread(size_t stack_size, std::function<void()> body) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    auto arg = new std::function<void()>(std::move(body));
    pthread_t thread;
    int err = pthread_create(&thread, &attr, thread_main, arg);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        delete arg;
        throw loader::LoadError("PE のスレッドを作成できませんでした");
    }
}

}
