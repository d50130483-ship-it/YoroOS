/* YoroOS 0.2 - Coalescing 16-byte aligned kernel heap (32-bit & 64-bit clean) */
#include "kernel.h"

#define HEAP_MAGIC 0x594F4832U

typedef struct BlockHdr {
    u32 magic;
    u32 free;
    size_t size;
    struct BlockHdr *next;
    struct BlockHdr *prev;
} BlockHdr;

#define HDR_SIZE ((sizeof(BlockHdr) + 15U) & ~(size_t)15U)

static BlockHdr *heap_head = NULL;
static size_t heap_bytes_total = 0;
static size_t heap_bytes_used = 0;

void heap_init(void *start, size_t size) {
    uptr aligned = ((uptr)start + 15U) & ~(uptr)15U;
    size_t delta = (size_t)(aligned - (uptr)start);
    if (size <= delta + HDR_SIZE + 64) return;
    size -= delta;
    heap_head = (BlockHdr *)aligned;
    heap_head->magic = HEAP_MAGIC;
    heap_head->free = 1;
    heap_head->size = size - HDR_SIZE;
    heap_head->next = NULL;
    heap_head->prev = NULL;
    heap_bytes_total = size;
    heap_bytes_used = 0;
}

void *kmalloc(size_t n) {
    BlockHdr *b;
    if (!heap_head || n == 0) return NULL;
    n = (n + 15U) & ~(size_t)15U;
    for (b = heap_head; b; b = b->next) {
        if (b->free && b->size >= n) {
            if (b->size >= n + HDR_SIZE + 32U) {
                BlockHdr *nb = (BlockHdr *)((u8 *)b + HDR_SIZE + n);
                nb->magic = HEAP_MAGIC;
                nb->free = 1;
                nb->size = b->size - n - HDR_SIZE;
                nb->next = b->next;
                nb->prev = b;
                if (b->next) b->next->prev = nb;
                b->next = nb;
                b->size = n;
            }
            b->free = 0;
            heap_bytes_used += b->size + HDR_SIZE;
            return (u8 *)b + HDR_SIZE;
        }
    }
    return NULL;
}

void *kzalloc(size_t n) {
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void *krealloc(void *p, size_t n) {
    BlockHdr *b;
    void *np;
    if (!p) return kmalloc(n);
    if (n == 0) { kfree(p); return NULL; }
    b = (BlockHdr *)((u8 *)p - HDR_SIZE);
    if (b->magic != HEAP_MAGIC) return NULL;
    if (b->size >= n) return p;
    np = kmalloc(n);
    if (!np) return NULL;
    memcpy(np, p, b->size);
    kfree(p);
    return np;
}

void kfree(void *p) {
    BlockHdr *b;
    if (!p) return;
    b = (BlockHdr *)((u8 *)p - HDR_SIZE);
    if (b->magic != HEAP_MAGIC || b->free) return;
    b->free = 1;
    if (heap_bytes_used >= b->size + HDR_SIZE) heap_bytes_used -= b->size + HDR_SIZE;
    if (b->next && b->next->free) {
        BlockHdr *nx = b->next;
        b->size += HDR_SIZE + nx->size;
        b->next = nx->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->free) {
        BlockHdr *pv = b->prev;
        pv->size += HDR_SIZE + b->size;
        pv->next = b->next;
        if (b->next) b->next->prev = pv;
    }
}

size_t heap_used(void) { return heap_bytes_used; }
size_t heap_total(void) { return heap_bytes_total; }
