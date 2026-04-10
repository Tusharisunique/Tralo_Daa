#pragma once

#include "../../Order Matching Engine/include/Types.h"
#include <vector>
#include <atomic>
#include <mutex>

class OrderQueue {
private:
    std::vector<Order> buffer;
    size_t capacity;
    size_t head = 0;
    size_t tail = 0;
    std::mutex mtx; // Used for queue thread-safety, using try_lock to simulate lock-free / latency non-block

public:
    explicit OrderQueue(size_t cap = 100000) : capacity(cap) {
        buffer.resize(capacity);
    }

    // Attempt to enqueue an order. Now blocking to ensure zero drops.
    bool try_enqueue(const Order& order) {
        mtx.lock();

        size_t next_head = (head + 1) % capacity;
        if (next_head == tail) {
            mtx.unlock();
            return false; // Queue truly full
        }

        buffer[head] = order;
        head = next_head;
        mtx.unlock();
        return true;
    }

    // Try to dequeue an order. Returns false if the queue is empty.
    bool try_dequeue(Order& out_order) {
        std::lock_guard<std::mutex> lock(mtx);
        if (head == tail) {
            return false; // Empty
        }

        out_order = buffer[tail];
        tail = (tail + 1) % capacity;
        return true;
    }
};
