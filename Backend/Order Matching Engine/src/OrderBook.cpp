// Implementation of Price-Time Priority order matching utilizing O(1) structures.
#include "OrderBook.h"
#include <cstdint>
#include <chrono>

using namespace std;

OrderBook::OrderBook(uint64_t max_price, function<void(const Trade&)> on_trade)
    : best_bid(0), best_ask(UINT64_MAX), trade_callback(on_trade) {
    bids.resize(max_price + 1);
    asks.resize(max_price + 1);
}

void OrderBook::add_order_to_level(PriceLevel& level, OrderNode* node) {
    node->next = nullptr;
    node->prev = level.tail;
    if (level.tail) {
        level.tail->next = node;
    } else {
        level.head = node;
    }
    level.tail = node;
}

void OrderBook::remove_order_from_level(PriceLevel& level, OrderNode* node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        level.head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        level.tail = node->prev;
    }
    node->next = nullptr;
    node->prev = nullptr;
}

static uint64_t now_ns() {
    return static_cast<uint64_t>(
        chrono::high_resolution_clock::now().time_since_epoch().count());
}

void OrderBook::process_order(const Order& incoming_order) {
    Order order = incoming_order;
    if (order.timestamp == 0) order.timestamp = now_ns();

    if (order.side == Side::BUY) {
        while (order.quantity > 0 && best_ask != UINT64_MAX && best_ask <= order.price) {
            if (best_ask >= asks.size()) { best_ask = UINT64_MAX; break; }

            PriceLevel& level = asks[best_ask];
            while (level.head && order.quantity > 0) {
                OrderNode* current = level.head;
                uint32_t fill_qty = min(order.quantity, current->order.quantity);

                Trade trade{};
                trade.maker_order_id = current->order.order_id;
                trade.taker_order_id = order.order_id;
                trade.maker_trader_id = current->order.trader_id;
                trade.taker_trader_id = order.trader_id;
                trade.quantity       = fill_qty;
                trade.price          = current->order.price;
                trade.timestamp      = now_ns();
                strncpy(trade.symbol, order.symbol, sizeof(trade.symbol));
                trade_callback(trade);

                order.quantity           -= fill_qty;
                current->order.quantity  -= fill_qty;

                if (current->order.quantity == 0) {
                    remove_order_from_level(level, current);
                    order_pool.deallocate(current);
                }
            }

            if (!level.head) {
                best_ask++;
                while (best_ask < asks.size() && !asks[best_ask].head) {
                    best_ask++;
                }
                if (best_ask >= asks.size()) best_ask = UINT64_MAX;
            } else {
                break; 
            }
        }

        if (order.quantity > 0) {
            if (order.price < bids.size()) {
                OrderNode* node = order_pool.allocate();
                if (node) {
                    node->order = order;
                    node->next  = nullptr;
                    node->prev  = nullptr;
                    add_order_to_level(bids[order.price], node);
                    if (order.price > best_bid) best_bid = order.price;
                }
            }
        }
    } else {
        while (order.quantity > 0 && best_bid > 0 && best_bid >= order.price) {
            if (best_bid >= bids.size()) { best_bid = 0; break; }

            PriceLevel& level = bids[best_bid];
            while (level.head && order.quantity > 0) {
                OrderNode* current = level.head;
                uint32_t fill_qty = min(order.quantity, current->order.quantity);

                Trade trade{};
                trade.maker_order_id = current->order.order_id;
                trade.taker_order_id = order.order_id;
                trade.maker_trader_id = current->order.trader_id;
                trade.taker_trader_id = order.trader_id;
                trade.quantity       = fill_qty;
                trade.price          = current->order.price;
                trade.timestamp      = now_ns();
                strncpy(trade.symbol, order.symbol, sizeof(trade.symbol));
                trade_callback(trade);

                order.quantity           -= fill_qty;
                current->order.quantity  -= fill_qty;

                if (current->order.quantity == 0) {
                    remove_order_from_level(level, current);
                    order_pool.deallocate(current);
                }
            }

            if (!level.head) {
                if (best_bid == 0) break;
                best_bid--;
                while (best_bid > 0 && !bids[best_bid].head) {
                    best_bid--;
                }
            } else {
                break; 
            }
        }

        if (order.quantity > 0) {
            if (order.price < asks.size()) {
                OrderNode* node = order_pool.allocate();
                if (node) {
                    node->order = order;
                    node->next  = nullptr;
                    node->prev  = nullptr;
                    add_order_to_level(asks[order.price], node);
                    if (order.price < best_ask) best_ask = order.price;
                }
            }
        }
    }
}
