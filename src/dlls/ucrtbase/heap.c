#include "ucrtbase_private.h"

static HANDLE process_heap;

void heap_init(void) {
    process_heap = GetProcessHeap();
}

void* __cdecl malloc(size_t size) {
    void* p = HeapAlloc(process_heap, 0, size ? size : 1);
    if (!p) *_errno() = ENOMEM;
    return p;
}

void* __cdecl calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size) {
        *_errno() = ENOMEM;
        return NULL;
    }
    size_t total = count * size;
    void* p = HeapAlloc(process_heap, HEAP_ZERO_MEMORY, total ? total : 1);
    if (!p) *_errno() = ENOMEM;
    return p;
}

void* __cdecl realloc(void* p, size_t size) {
    if (!p) return malloc(size);
    if (size == 0) {
        /* UCRT では size が 0 なら解放して NULL を返す */
        free(p);
        return NULL;
    }
    void* q = HeapReAlloc(process_heap, 0, p, size);
    if (!q) *_errno() = ENOMEM;
    return q;
}

void __cdecl free(void* p) {
    if (p) HeapFree(process_heap, 0, p);
}

size_t __cdecl _msize(void* p) {
    return HeapSize(process_heap, 0, p);
}
