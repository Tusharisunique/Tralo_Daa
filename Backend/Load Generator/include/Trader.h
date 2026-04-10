#pragma once

#include "../../Order Matching Engine/include/Types.h"
#include "OrderQueue.h"
#include <thread>
#include <atomic>
#include <random>
#include <chrono>

struct TraderStats {
    uint64_t orders_generated = 0;
    uint64_t orders_dropped = 0;
};

class Trader {
protected:
    uint32_t trader_id;
    OrderQueue& queue;
    std::atomic<bool> running{false};
    std::thread worker_thread;
    
    uint64_t local_generated = 0;
    uint64_t local_dropped = 0;
    
    std::atomic<uint64_t>& global_generated;
    std::atomic<uint64_t>& global_dropped;
    std::vector<std::string> symbols;

    std::mt19937 rng;

    uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
    }

    virtual void run() = 0;

public:
    Trader(uint32_t id, OrderQueue& q, std::atomic<uint64_t>& g_gen, std::atomic<uint64_t>& g_drop, const std::vector<std::string>& syms)
        : trader_id(id), queue(q), global_generated(g_gen), global_dropped(g_drop), symbols(syms) {
        std::random_device rd;
        rng.seed(rd() ^ (id << 16)); 
    }

    virtual ~Trader() {
        stop();
    }

    void start() {
        if (!running.exchange(true)) {
            worker_thread = std::thread(&Trader::run, this);
        }
    }

    void stop() {
        if (running.exchange(false)) {
            if (worker_thread.joinable()) {
                worker_thread.join();
            }
        }
    }

    TraderStats get_stats() const {
        return {local_generated, local_dropped};
    }
};
