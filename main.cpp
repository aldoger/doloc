#include <cstddef>
#include <mutex>
#include "unordered_map"
#include <stddef.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

std::mutex g_allocator_mutes;

struct Header {
    size_t size;
    struct Header *next;
};

void* allocate(size_t size) {
    std::lock_guard<std::mutex> lock(g_allocator_mutes);
}

class MyAllocator {
public:
    // Public interface
    void* allocate(size_t size);
    void deallocate(void* ptr);

private:
    // The hidden header stored just before the user's memory block.
    // This is how we find the size during deallocation.
    struct BlockHeader {
        size_t size;
    };

    struct FreeBlockHeader {
        FreeBlockHeader* next;
    };

    // A contiguous run of one or more pages managed by the PageHeap.
    struct Span {
        size_t start_page_id;
        size_t num_pages;
        Span* next = nullptr;
        Span* prev = nullptr;
        bool is_free = true;
    };

    // Shared buffer between ThreadCache and PageHeap to reduce lock contention.
    struct TransferCache {
        std::mutex mtx;
        FreeBlockHeader* list = nullptr;
        int count = 0;
    };

    // Per-thread private cache for small allocations.
    struct ThreadCache {
        FreeBlockHeader* free_lists[8] = {nullptr};
        int list_lengths[8] = {0};
    };


    // Tier 3: The global page-level allocator.
    class PageHeap {
    public:
        Span* allocateSpan(size_t num_pages);
        void deallocateSpan(Span* span);
        Span* lookupSpan(void* ptr);
    private:
        std::mutex mtx;
        Span* free_spans[256] = {nullptr}; // Free lists for spans of 1-255 pages
        std::unordered_map<size_t, Span*> page_map;
    };

    PageHeap page_heap; // Tier 3
    TransferCache transfer_caches[8]; // Tier 2

    static size_t getSizeClassIndex(size_t size);
    static size_t getClassSizeFromIndex(size_t index);

    // Moves objects from TransferCache to ThreadCache.
    void fetchFromTransferCache(size_t class_index);
    // Scavenging: Moves objects from ThreadCache back to TransferCache.
    void releaseToTransferCache(size_t class_index);
};
