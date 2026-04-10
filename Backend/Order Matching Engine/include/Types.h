// Core data structures for the Order Matching Engine including Orders and Trades.
#pragma once

#include <cstdint>

enum class Side : uint8_t {
    BUY,
    SELL
};

struct Order {
    uint64_t order_id;
    uint32_t trader_id;
    uint32_t quantity;
    uint64_t price;
    uint64_t timestamp;
    Side side;
    char symbol[12];
    uint8_t _pad[3]; 
} __attribute__((aligned(8)));

struct Trade {
    uint64_t maker_order_id;
    uint64_t taker_order_id;
    uint32_t maker_trader_id;
    uint32_t taker_trader_id;
    uint32_t quantity;
    uint64_t price;
    uint64_t timestamp;
    char symbol[12];
};

struct OrderNode {
    Order order;
    OrderNode* next;
    OrderNode* prev;
};
