// The high-performance matching interface managing the flat price arrays and memory allocations.
#pragma once

#include "Types.h"
#include "MemoryPool.h"
#include <vector>
#include <functional>

struct PriceLevel {
    OrderNode* head = nullptr;
    OrderNode* tail = nullptr;
};

class OrderBook {
private:
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    
    uint64_t best_bid;
    uint64_t best_ask;
    
    MemoryPool<OrderNode, 1000000> order_pool;
    
    std::function<void(const Trade&)> trade_callback;

    void add_order_to_level(PriceLevel& level, OrderNode* node);
    void remove_order_from_level(PriceLevel& level, OrderNode* node);
    
public:
    OrderBook(uint64_t max_price, std::function<void(const Trade&)> on_trade);
    
    void process_order(const Order& order);

    uint64_t get_best_bid() const { return best_bid; }
    uint64_t get_best_ask() const { return best_ask; }
};
