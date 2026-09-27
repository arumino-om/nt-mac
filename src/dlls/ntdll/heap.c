#include "ntdll/ntdll.h"
#include "shared/ntstatus.h"

/*
 * 簡易的なヒープ．
 *
 * - 64KB 以下の確保は 2 のべき乗のサイズクラスに切り上げ，クラスごとのフリーリストで再利用する
 *   (ブロックは 1MB 単位で仮想メモリから切り出し，OS には返さない)
 * - それより大きい確保は NtAllocateVirtualMemory で直接確保し，解放時に返す
 * - 各ブロックの前に 16 バイトのヘッダを置き，返すアドレスは 16 バイト境界に揃える (Windows x64 と同じ)
 */

#define HEAP_MAGIC        0x4E544D48u /* "NTMH" */
#define BLOCK_MAGIC       0x4B4C4Bu
#define MIN_CLASS_SHIFT   4           /* 16 バイト */
#define MAX_CLASS_SHIFT   16          /* 64KB */
#define NUM_CLASSES       (MAX_CLASS_SHIFT - MIN_CLASS_SHIFT + 1)
#define LARGE_CLASS       0xFFu
#define ARENA_SIZE        (1u << 20)

typedef struct block_header {
    SIZE_T size;        /* 要求されたサイズ (RtlSizeHeap が返す値) */
    uint32_t magic : 24;
    uint32_t cls : 8;   /* サイズクラス．LARGE_CLASS なら仮想メモリを直接確保したブロック */
    uint32_t _pad;
} block_header;

typedef struct free_block {
    struct free_block* next;
} free_block;

typedef struct heap {
    uint32_t magic;
    volatile LONG lock;
    free_block* free_lists[NUM_CLASSES];
    uint8_t* arena_cur;
    uint8_t* arena_end;
} heap;

_Static_assert(sizeof(block_header) == 16, "block header must keep 16-byte alignment");

static void* virtual_alloc(SIZE_T size) {
    PVOID base = NULL;
    SIZE_T region = size;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &region,
                                            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))) {
        return NULL;
    }
    return base;
}

static void virtual_free(void* p) {
    PVOID base = p;
    SIZE_T size = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, MEM_RELEASE);
}

static void lock_heap(heap* h) {
    while (__atomic_exchange_n(&h->lock, 1, __ATOMIC_ACQUIRE)) NtYieldExecution();
}

static void unlock_heap(heap* h) {
    __atomic_store_n(&h->lock, 0, __ATOMIC_RELEASE);
}

static int size_class(SIZE_T size) {
    int cls = 0;
    while (((SIZE_T)1 << (cls + MIN_CLASS_SHIFT)) < size) cls++;
    return cls;
}

static SIZE_T class_size(int cls) {
    return (SIZE_T)1 << (cls + MIN_CLASS_SHIFT);
}

static block_header* header_of(heap* h, PVOID p) {
    if (!h || h->magic != HEAP_MAGIC || !p || ((ULONG_PTR)p & 15)) return NULL;
    block_header* hdr = (block_header*)p - 1;
    return hdr->magic == BLOCK_MAGIC ? hdr : NULL;
}

/* ロックを取得した状態で呼ぶこと */
static void* alloc_small(heap* h, int cls) {
    free_block* b = h->free_lists[cls];
    if (b) {
        h->free_lists[cls] = b->next;
        return b;
    }

    SIZE_T need = sizeof(block_header) + class_size(cls);
    if ((SIZE_T)(h->arena_end - h->arena_cur) < need) {
        uint8_t* arena = virtual_alloc(ARENA_SIZE);
        if (!arena) return NULL;
        h->arena_cur = arena;
        h->arena_end = arena + ARENA_SIZE;
    }

    block_header* hdr = (block_header*)h->arena_cur;
    h->arena_cur += need;
    hdr->cls = (uint32_t)cls;
    return hdr + 1;
}

PVOID NTAPI RtlCreateHeap(ULONG Flags, PVOID HeapBase, SIZE_T ReserveSize, SIZE_T CommitSize,
                          PVOID Lock, PVOID Parameters) {
    (void)Flags;
    (void)ReserveSize;
    (void)CommitSize;
    (void)Lock;
    (void)Parameters;
    if (HeapBase) return NULL; /* 呼び出し側が用意した領域をヒープにするのは未対応 */

    heap* h = virtual_alloc(sizeof(heap));
    if (!h) return NULL;
    h->magic = HEAP_MAGIC; /* 他のフィールドは virtual_alloc によりゼロ */
    return h;
}

PVOID NTAPI RtlAllocateHeap(PVOID HeapHandle, ULONG Flags, SIZE_T Size) {
    heap* h = HeapHandle;
    if (!h || h->magic != HEAP_MAGIC) return NULL;
    if (Size == 0) Size = 1;

    block_header* hdr;
    if (Size > class_size(NUM_CLASSES - 1)) {
        if (Size > (SIZE_T)-1 - sizeof(block_header)) return NULL;
        hdr = virtual_alloc(sizeof(block_header) + Size); /* 仮想メモリはゼロで埋まっている */
        if (!hdr) return NULL;
        hdr->cls = LARGE_CLASS;
        hdr += 1;
    } else {
        lock_heap(h);
        hdr = alloc_small(h, size_class(Size));
        unlock_heap(h);
        if (!hdr) return NULL;
        if (Flags & HEAP_ZERO_MEMORY) memset(hdr, 0, Size);
    }

    hdr[-1].size = Size;
    hdr[-1].magic = BLOCK_MAGIC;
    return hdr;
}

BOOLEAN NTAPI RtlFreeHeap(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress) {
    (void)Flags;
    if (!BaseAddress) return TRUE;

    heap* h = HeapHandle;
    block_header* hdr = header_of(h, BaseAddress);
    if (!hdr) return FALSE;
    hdr->magic = 0;

    if (hdr->cls == LARGE_CLASS) {
        virtual_free(hdr);
        return TRUE;
    }

    lock_heap(h);
    free_block* b = BaseAddress;
    b->next = h->free_lists[hdr->cls];
    h->free_lists[hdr->cls] = b;
    unlock_heap(h);
    return TRUE;
}

PVOID NTAPI RtlReAllocateHeap(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress, SIZE_T Size) {
    heap* h = HeapHandle;
    block_header* hdr = header_of(h, BaseAddress);
    if (!hdr) return NULL;
    if (Size == 0) Size = 1;

    SIZE_T old = hdr->size;
    SIZE_T capacity = hdr->cls == LARGE_CLASS ? old : class_size(hdr->cls);
    if (Size <= capacity) {
        if ((Flags & HEAP_ZERO_MEMORY) && Size > old) memset((uint8_t*)BaseAddress + old, 0, Size - old);
        hdr->size = Size;
        return BaseAddress;
    }
    if (Flags & HEAP_REALLOC_IN_PLACE_ONLY) return NULL;

    void* p = RtlAllocateHeap(HeapHandle, Flags, Size);
    if (!p) return NULL;
    memcpy(p, BaseAddress, old);
    RtlFreeHeap(HeapHandle, 0, BaseAddress);
    return p;
}

SIZE_T NTAPI RtlSizeHeap(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress) {
    (void)Flags;
    block_header* hdr = header_of(HeapHandle, BaseAddress);
    return hdr ? hdr->size : (SIZE_T)-1;
}
