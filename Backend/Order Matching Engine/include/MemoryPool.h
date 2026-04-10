// A static, single-threaded object pool allocator for O(1) allocation without system calls.
#pragma once

#include <array>
#include <cstddef>

template<typename T, size_t Size>
class MemoryPool {
private:
    std::array<T, Size> data;
    std::array<T*, Size> free_list;
    size_t free_index;

public:
    MemoryPool() : free_index(Size) {
        for (size_t i = 0; i < Size; ++i) {
            free_list[i] = &data[i];
        }
    }

    T* allocate() {
        if (free_index == 0) return nullptr;
        return free_list[--free_index];
    }

    void deallocate(T* ptr) {
        free_list[free_index++] = ptr;
    }
};
