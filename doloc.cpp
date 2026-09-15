#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <utility>

class Arena {
public:
    explicit Arena(std::size_t size)
        : buffer_(static_cast<std::byte*>(::operator new(size))),
          capacity_(size), offset_(0) {
        // Keeping unused bytes zeroed makes the buffer dump deterministic.
        std::memset(buffer_, 0, capacity_);
    }

    ~Arena() {
        ::operator delete(buffer_);
    }

    void* allocate(std::size_t size, std::size_t alignment) {
        void* current = buffer_ + offset_;
        std::size_t remaining = capacity_ - offset_;

        void* aligned = std::align(alignment, size, current, remaining);
        if (aligned == nullptr) {
            throw std::bad_alloc();
        }

        offset_ = static_cast<std::byte*>(aligned) - buffer_ + size;
        return aligned;
    }

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        void* memory = allocate(sizeof(T), alignof(T));
        return ::new (memory) T(std::forward<Args>(args)...);
    }

    void print_buffer() const {
        std::cout << "Buffer (" << offset_ << "/" << capacity_ << " bytes used):\n";
        for (std::size_t i = 0; i < offset_; ++i) {
            if (i % 16 == 0) {
                std::cout << "  " << std::setw(4) << std::setfill('0') << i << ": ";
            }

            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << std::to_integer<unsigned int>(buffer_[i]) << ' ';

            if (i % 16 == 15 || i + 1 == offset_) {
                std::cout << '\n';
            }
        }
        std::cout << std::dec << std::setfill(' ');
    }

    void reset() {
        offset_ = 0;
        std::memset(buffer_, 0, capacity_);
    }

    Arena(const Arena&) = delete;
    Arena(Arena&&) = delete;

private:
    std::byte* buffer_;
    std::size_t capacity_;
    std::size_t offset_;
};

int main() {
    Arena arena(64);

    int* count = arena.make<int>(42);
    std::cout << "Allocated int: " << *count << '\n';
    arena.print_buffer();

    double* price = arena.make<double>(19.99);
    std::cout << "Allocated double: " << *price << '\n';
    arena.print_buffer();

    char* label = static_cast<char*>(arena.allocate(6, alignof(char)));
    std::memcpy(label, "Arena", 6);
    std::cout << "Allocated label: " << label << '\n';
    arena.print_buffer();
}
