/*
 * linked_list_allocator.c
 *
 * A simple malloc/free implementation using an EXPLICIT FREE LIST
 * (a doubly linked list threaded through the free blocks themselves).
 *
 * Design:
 *  - Memory is requested from the OS via sbrk() in chunks as needed.
 *  - Every block (free or allocated) has a header with size + bookkeeping.
 *  - Free blocks are linked together via next_free/prev_free pointers
 *    stored INSIDE the free block's own memory (no extra allocation needed).
 *  - Blocks also form an implicit physical list (next_phys/prev_phys) so
 *    that neighbours in memory can be found and merged (coalesced).
 *  - Allocation uses first-fit search over the free list.
 *  - Freeing a block coalesces it with adjacent free blocks to fight
 *    fragmentation.
 *
 * This is a teaching implementation: NOT thread-safe, and it never
 * returns memory to the OS (only reuses it internally). Real allocators
 * (ptmalloc, jemalloc, tcmalloc) add thread-local arenas, size-class
 * bins, and OS memory release on top of these same core ideas.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

/* ---------- Alignment ---------- */
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

/* ---------- Block header ---------- */
typedef struct block_header {
    size_t size;                 /* size of usable data area (excludes header) */
    int free;                    /* 1 if this block is free */

    struct block_header *next_phys; /* next block in memory address order */
    struct block_header *prev_phys; /* previous block in memory address order */

    struct block_header *next_free; /* next block in the free list (only valid if free) */
    struct block_header *prev_free; /* previous block in the free list (only valid if free) */
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)
#define MIN_SPLIT_SIZE (HEADER_SIZE + ALIGNMENT) /* smallest useful leftover after a split */

/* ---------- Global allocator state ---------- */
static block_header_t *free_list_head = NULL; /* head of the explicit free list */
static block_header_t *heap_start     = NULL; /* first block ever created */
static block_header_t *heap_end       = NULL; /* physically last block */

/* ================= Free list helpers ================= */

static void remove_from_free_list(block_header_t *b) {
    if (b->prev_free)
        b->prev_free->next_free = b->next_free;
    else
        free_list_head = b->next_free;

    if (b->next_free)
        b->next_free->prev_free = b->prev_free;

    b->next_free = NULL;
    b->prev_free = NULL;
}

static void add_to_free_list(block_header_t *b) {
    b->free = 1;
    b->prev_free = NULL;
    b->next_free = free_list_head;
    if (free_list_head)
        free_list_head->prev_free = b;
    free_list_head = b;
}

/* ================= Getting memory from the OS ================= */

/* Extend the heap with a brand new block of `size` usable bytes. */
static block_header_t *request_space(block_header_t *last_phys, size_t size) {
    block_header_t *block = (block_header_t *)sbrk(0); /* current break = new block's address */

    void *request = sbrk((intptr_t)(HEADER_SIZE + size));
    if (request == (void *)-1)
        return NULL; /* sbrk failed: out of memory */

    block->size = size;
    block->free = 0;
    block->next_phys = NULL;
    block->prev_phys = last_phys;
    block->next_free = NULL;
    block->prev_free = NULL;

    if (last_phys)
        last_phys->next_phys = block;

    return block;
}

/* ================= Search, split, coalesce ================= */

/* First-fit: return the first free block big enough, or NULL. */
static block_header_t *find_free_block(size_t size) {
    block_header_t *cur = free_list_head;
    while (cur) {
        if (cur->size >= size)
            return cur;
        cur = cur->next_free;
    }
    return NULL;
}

/* If `block` is much bigger than `size`, carve off the remainder as a
 * new free block right after it. */
static void split_block(block_header_t *block, size_t size) {
    if (block->size < size + MIN_SPLIT_SIZE)
        return; /* not worth splitting */

    char *data_start = (char *)(block + 1);
    block_header_t *new_block = (block_header_t *)(data_start + size);

    new_block->size = block->size - size - HEADER_SIZE;
    new_block->next_phys = block->next_phys;
    new_block->prev_phys = block;

    if (block->next_phys)
        block->next_phys->prev_phys = new_block;
    if (heap_end == block)
        heap_end = new_block;

    block->next_phys = new_block;
    block->size = size;

    add_to_free_list(new_block);
}

/* Merge `block` with its physical neighbours if they are free too.
 * Returns the (possibly different) header of the merged block. */
static block_header_t *coalesce(block_header_t *block) {
    /* merge with next block */
    if (block->next_phys && block->next_phys->free) {
        block_header_t *next = block->next_phys;
        remove_from_free_list(next);

        block->size += HEADER_SIZE + next->size;
        block->next_phys = next->next_phys;
        if (next->next_phys)
            next->next_phys->prev_phys = block;
        if (heap_end == next)
            heap_end = block;
    }

    /* merge with previous block */
    if (block->prev_phys && block->prev_phys->free) {
        block_header_t *prev = block->prev_phys;
        remove_from_free_list(prev);

        prev->size += HEADER_SIZE + block->size;
        prev->next_phys = block->next_phys;
        if (block->next_phys)
            block->next_phys->prev_phys = prev;
        if (heap_end == block)
            heap_end = prev;

        block = prev;
    }

    return block;
}

/* ================= Public API ================= */

void *my_malloc(size_t size) {
    if (size == 0)
        return NULL;

    size = ALIGN(size);
    block_header_t *block;

    if (!heap_start) {
        /* very first allocation ever */
        block = request_space(NULL, size);
        if (!block)
            return NULL;
        heap_start = block;
        heap_end = block;
    } else {
        block = find_free_block(size);
        if (block) {
            remove_from_free_list(block);
            split_block(block, size);
            block->free = 0;
        } else {
            /* no free block big enough: grow the heap */
            block = request_space(heap_end, size);
            if (!block)
                return NULL;
            heap_end = block;
        }
    }

    return (void *)(block + 1); /* hand back pointer just past the header */
}

static block_header_t *get_header(void *ptr) {
    return (block_header_t *)ptr - 1;
}

void my_free(void *ptr) {
    if (!ptr)
        return;

    block_header_t *block = get_header(ptr);
    block->free = 1;
    block = coalesce(block);
    add_to_free_list(block);
}

/* Bonus: realloc, since real programs need it too. */
void *my_realloc(void *ptr, size_t size) {
    if (!ptr)
        return my_malloc(size);
    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    block_header_t *block = get_header(ptr);
    if (block->size >= size)
        return ptr; /* already big enough */

    void *new_ptr = my_malloc(size);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, block->size);
    my_free(ptr);
    return new_ptr;
}

/* ================= Debug helper ================= */

void print_heap(void) {
    block_header_t *cur = heap_start;
    printf("---- heap layout ----\n");
    while (cur) {
        printf("  [%p] size=%-6zu %s\n",
               (void *)cur, cur->size, cur->free ? "FREE" : "USED");
        cur = cur->next_phys;
    }
    printf("----------------------\n");
}

/* ================= Demo ================= */

int main(void) {
    printf("Allocating a, b, c...\n");
    char *a = my_malloc(64);
    char *b = my_malloc(128);
    char *c = my_malloc(32);

    strcpy(a, "hello from block a");
    strcpy(b, "hello from block b");
    strcpy(c, "hi c");

    print_heap();

    printf("Freeing b...\n");
    my_free(b);
    print_heap();

    printf("Freeing a (should coalesce with the freed b)...\n");
    my_free(a);
    print_heap();

    printf("Allocating d (80 bytes) -- should reuse the coalesced a+b space...\n");
    char *d = my_malloc(80);
    strcpy(d, "reused space!");
    printf("d = \"%s\"\n", d);
    print_heap();

    printf("Freeing c and d...\n");
    my_free(c);
    my_free(d);
    print_heap();

    return 0;
}