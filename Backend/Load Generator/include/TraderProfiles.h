#pragma once

#include "Trader.h"
#include <iostream>
#include <cstring>
#include <map>

struct StockConfig {
    uint64_t base_price_paise;
    uint32_t spread_paise;
};

// Internal engine prices are in Paise (1/100th of a Rupee)
static std::map<std::string, StockConfig> STOCK_CONFIGS = {
    {"RELIANCE", {250000, 1000}},
    {"TCS",      {350000, 1500}},
    {"HDFC",     {160000, 800}},
    {"INFOSYS",  {150000, 800}},
    {"ICICIBANK",{95000,  500}}
};

class RetailTrader : public Trader {
public:
  using Trader::Trader;

protected:
  void run() override {
    // Retail: 100-500ms (Very active)
    std::uniform_int_distribution<int> sleep_dist(100, 500);
    std::uniform_int_distribution<uint32_t> qty_dist(5, 50);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> sym_dist(0, symbols.size() - 1);

    while (running.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_dist(rng)));

      const std::string& sym = symbols[sym_dist(rng)];
      const auto& config = STOCK_CONFIGS[sym];
      
      std::uniform_int_distribution<uint64_t> price_dist(config.base_price_paise - 500, config.base_price_paise + 500);

      Order o{};
      o.price = price_dist(rng);
      o.quantity = qty_dist(rng);
      o.side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
      o.trader_id = trader_id;
      o.timestamp = now_ns();
      strncpy(o.symbol, sym.c_str(), sizeof(o.symbol));

      local_generated++;
      global_generated.fetch_add(1, std::memory_order_relaxed);

      if (!queue.try_enqueue(o)) {
        local_dropped++;
        global_dropped.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
};

class HFTTrader : public Trader {
public:
  using Trader::Trader;

protected:
  void run() override {
    // HFT: 20-50ms (Aggressive)
    std::uniform_int_distribution<int> sleep_dist(20, 50); 
    std::uniform_int_distribution<uint32_t> qty_dist(50, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> sym_dist(0, symbols.size() - 1);

    while (running.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_dist(rng)));

      const std::string& sym = symbols[sym_dist(rng)];
      const auto& config = STOCK_CONFIGS[sym];
      std::normal_distribution<double> price_dist(config.base_price_paise, 100.0);

      Order o{};
      o.price = static_cast<uint64_t>(price_dist(rng));
      o.quantity = qty_dist(rng);
      o.side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
      o.trader_id = trader_id;
      o.timestamp = now_ns();
      strncpy(o.symbol, sym.c_str(), sizeof(o.symbol));

      local_generated++;
      global_generated.fetch_add(1, std::memory_order_relaxed);

      if (!queue.try_enqueue(o)) {
        local_dropped++;
        global_dropped.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
};

class MarketMaker : public Trader {
public:
  using Trader::Trader;

protected:
  void run() override {
    // MM: 50-200ms (Constant coverage)
    std::uniform_int_distribution<int> sleep_dist(50, 200);
    std::uniform_int_distribution<int> sym_dist(0, symbols.size() - 1);

    while (running.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_dist(rng)));

      const std::string& sym = symbols[sym_dist(rng)];
      const auto& config = STOCK_CONFIGS[sym];

      Order buy_o{}, sell_o{};
      buy_o.price = config.base_price_paise - 50; // Tight spread (50 paise)
      buy_o.quantity = 1000;
      buy_o.side = Side::BUY;
      buy_o.trader_id = trader_id;
      buy_o.timestamp = now_ns();
      strncpy(buy_o.symbol, sym.c_str(), sizeof(buy_o.symbol));

      sell_o.price = config.base_price_paise + 50;
      sell_o.quantity = 1000;
      sell_o.side = Side::SELL;
      sell_o.trader_id = trader_id;
      sell_o.timestamp = now_ns();
      strncpy(sell_o.symbol, sym.c_str(), sizeof(sell_o.symbol));

      local_generated += 2;
      global_generated.fetch_add(2, std::memory_order_relaxed);

      if (!queue.try_enqueue(buy_o)) {
        local_dropped++;
        global_dropped.fetch_add(1, std::memory_order_relaxed);
      }
      if (!queue.try_enqueue(sell_o)) {
        local_dropped++;
        global_dropped.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
};

class SniperTrader : public Trader {
public:
  using Trader::Trader;

protected:
  void run() override {
    std::uniform_int_distribution<int> sleep_dist(100, 300);
    std::uniform_int_distribution<int> burst_dist(5, 15);
    std::uniform_int_distribution<int> sym_dist(0, symbols.size() - 1);

    while (running.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(rng)));

      const std::string& sym = symbols[sym_dist(rng)];
      const auto& config = STOCK_CONFIGS[sym];
      std::uniform_int_distribution<uint64_t> price_dist(config.base_price_paise - 1000, config.base_price_paise + 1000);
      
      int burst_size = burst_dist(rng);
      for (int i = 0; i < burst_size && running.load(std::memory_order_relaxed); ++i) {
        Order o{};
        o.price = price_dist(rng);
        o.quantity = 10;
        o.side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        o.trader_id = trader_id;
        o.timestamp = now_ns();
        strncpy(o.symbol, sym.c_str(), sizeof(o.symbol));

        local_generated++;
        global_generated.fetch_add(1, std::memory_order_relaxed);

        if (!queue.try_enqueue(o)) {
          local_dropped++;
          global_dropped.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  }
};
