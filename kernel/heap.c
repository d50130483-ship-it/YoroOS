/* YoroOS - simple first-fit heap allocator over a contiguous region */
#include "kernel.h"

typedef struct Block {
    u32 size;            /* usable bytes */
    u32 magic;
    struct Block *next;
    struct Block *prev;
} Block;

#define MAGIC_USED 0x594F524F
#define MAGIC_FREE 0x46524545
#define ALIGN8(x) (((x) + 7u) & ~7u)

static Block *heap_head = NULL;
static u32 heap_size = 0;
static u32 heap_allocated = 0;

static void ensure_free(Block *b) { b->magic = MAGIC_FREE; }

void heap_init(u32 start, u32 size) {
    heap_size = size;
    heap_allocated = 0;
    heap_head = (Block *)start;
    heap_head->size = size - sizeof(Block);
    heap_head->next = NULL;
    heap_head->prev = NULL;
    ensure_free(heap_head);
}

void *kmalloc(size_t n) {
    Block *b = heap_head;
    if (!n) return NULL;
    n = ALIGN8(n);
    while (b) {
        if (b->magic == MAGIC_FREE && b->size >= n) {
            /* split if the remainder can hold a header */
            if (b->size >= n + sizeof(Block) + 16) {
                Block *nb = (Block *)((u8 *)b + sizeof(Block) + n);
                nb->size = b->size - n - sizeof(Block);
                nb->magic = MAGIC_FREE;
                nb->prev = b;
                nb->next = b->next;
                if (b->next) b->next->prev = nb;
                b->next = nb;
                b->size = n;
            }
            b->magic = MAGIC_USED;
            heap_allocated += b->size;
            return (u8 *)b + sizeof(Block);
        }
        b = b->next;
    }
    return NULL;
}

void *kzalloc(size_t n) {
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void *p) {
    Block *b;
    if (!p) return;
    b = (Block *)((u8 *)p - sizeof(Block));
    if (b->magic != MAGIC_USED) return;
    heap_allocated -= b->size;
    b->magic = MAGIC_FREE;
    /* merge with next */
    while (b->next && b->next->magic == MAGIC_FREE) {
        Block *nb = b->next;
        b->size += sizeof(Block) + nb->size;
        b->next = nb->next;
        if (nb->next) nb->next->prev = b;
    }
    /* merge with previous */
    if (b->prev && b->prev->magic == MAGIC_FREE) {
        Block *pb = b->prev;
        pb->size += sizeof(Block) + b->size;
        pb->next = b->next;
        if (b->next) b->next->prev = pb;
    }
}

u32 heap_used(void) { return heap_allocated; }
u32 heap_total(void) { return heap_size; }
