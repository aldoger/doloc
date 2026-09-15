#pragma once
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <memory.h>

class Arena {

    public:
        explicit Arena(std::size_t size)
            : buffer_(static_cast<char*>(::operator new(size))),
            capacity_(size), offset_(0) {}

        ~Arena() {
            ::operator delete(buffer_);
        }

        void *allocate(std::size_t size, std::size_t alignment) {
            char *current_ptr = buffer_ + offset_;
            std::size_t space = capacity_ + offset_;
            void* aligned_ptr = current_ptr;

            if(std::align(alignment, size, aligned_ptr, space)==nullptr) {
                throw std::bad_alloc();
            }

            offset_ += static_cast<char*>(aligned_ptr) - buffer_ + size;

            return aligned_ptr;
        }

        void reset() {
            offset_ = 0;
        }

        Arena(const Arena&) = delete;
        Arena(Arena&&) = delete;

    private:
        char *buffer_;
        std::size_t capacity_;
        std::size_t offset_;
}
