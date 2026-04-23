#pragma once

#include "../../Order Matching Engine/include/Types.h"
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

class OrderQueue {
private:
    std::vector<Order> buffer;
    size_t capacity;
    size_t head = 0;
    size_t tail = 0;
    std::mutex mtx; 

public:
    explicit OrderQueue(size_t cap = 100000) : capacity(cap) {
        buffer.resize(capacity);
    }

    // Attempt to enqueue an order. Sets timestamp inside the lock for zero-bias measurement.
    bool try_enqueue(Order& order) {
        std::lock_guard<std::mutex> lock(mtx);

        size_t next_head = (head + 1) % capacity;
        if (next_head == tail) {
            return false; // Queue truly full
        }

        // Capture placement time INSIDE the lock to exclude mutex contention from latency.
        order.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();

        buffer[head] = order;
        head = next_head;
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
