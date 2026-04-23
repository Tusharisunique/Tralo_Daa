// Unified main connecting Load Generator, Order Matching Engine, and Fairness Analyzer.
#include "../Load Generator/include/LoadGenerator.h"
#include "../Load Generator/include/OrderQueue.h"
#include "../Order Matching Engine/include/OrderBook.h"
#include "../Fairness Analyzer/include/FairnessAnalyzer.h"
#include <iostream>
#include <thread>
#include <memory>
#include <atomic>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <cstring>

#include <mutex>

using namespace std;

// Thread-safe output to prevent JSON corruption
mutex cout_mutex;
void safe_print(const string& json) {
    lock_guard<mutex> lock(cout_mutex);
    cout << json << endl;
}

atomic<uint64_t> global_order_id{1};
atomic<uint64_t> global_trade_count{0};
FairnessAnalyzer analyzer;

// Global set of symbols
vector<string> ACTIVE_SYMBOLS = {"RELIANCE", "TCS", "HDFC", "INFOSYS", "ICICIBANK"};

void on_trade(const Trade& trade) {
    static atomic<uint64_t> bot_trade_counter{0};
    bool is_manual = (trade.maker_trader_id == 99999 || trade.taker_trader_id == 99999);

    // ALWAYS record match for fairness analysis (decoupled from print throttle)
    uint64_t latency = analyzer.on_match(trade.maker_order_id, trade.taker_order_id, trade.timestamp);
    global_trade_count.fetch_add(1, memory_order_relaxed);

    // Only PRINT matches if manual or sampled bots (to avoid flooding stdout)
    if (is_manual || (bot_trade_counter.fetch_add(1) % 5000 == 0)) {
        stringstream ss;
        ss << "{\"type\":\"trade\","
           << "\"symbol\":\"" << trade.symbol << "\","
           << "\"maker\":" << trade.maker_order_id
           << ",\"taker\":" << trade.taker_order_id
           << ",\"qty\":" << trade.quantity
           << ",\"price\":" << trade.price
           << ",\"latency_ns\":" << latency << "}";
        safe_print(ss.str());
    }
}

void stdin_reader(OrderQueue& queue) {
    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        istringstream ss(line);
        string side_str, sym;
        uint64_t price;
        uint32_t qty;
        uint32_t trader_id;
        
        // Expected format: BUY RELIANCE 2500 10 99999
        if (!(ss >> side_str >> sym >> price >> qty >> trader_id)) continue;

        // Price Band Check (±20% of base price)
        static const map<string, uint64_t> BASE_PRICES = {
            {"RELIANCE", 250000}, {"TCS", 350000}, {"HDFC", 160000},
            {"INFOSYS", 150000},  {"ICICIBANK", 95000}
        };

        bool rejected = false;
        string reason;

        if (BASE_PRICES.count(sym)) {
            uint64_t base = BASE_PRICES.at(sym);
            uint64_t lower = base * 0.8;
            uint64_t upper = base * 1.2;
            if (price < lower || price > upper) {
                rejected = true;
                reason = "Out of Price Bands (Circuit Breaker)";
            }
        }

        if (rejected) {
            stringstream ss_rej;
            ss_rej << "{\"type\":\"reject\",\"symbol\":\"" << sym 
                   << "\",\"reason\":\"" << reason << "\"}";
            safe_print(ss_rej.str());
            continue;
        }

        Order o{};
        o.order_id  = global_order_id.fetch_add(1, memory_order_relaxed);
        o.price     = price;
        o.quantity  = qty;
        o.trader_id = trader_id;
        o.side      = (side_str == "BUY") ? Side::BUY : Side::SELL;
        o.timestamp = 0; // Set inside OrderQueue::try_enqueue for zero-bias measurement
        strncpy(o.symbol, sym.c_str(), sizeof(o.symbol));

        // Note: analyzer.on_order_arrival is called in the main loop for ALL orders
        // to ensure zero bias in when the 'arrival' is recorded relative to dequeue.
        stringstream ss_ack;
        ss_ack << "{\"type\":\"ack\",\"order_id\":" << o.order_id
             << ",\"symbol\":\"" << sym << "\""
             << ",\"side\":\"" << side_str << "\""
             << ",\"price\":" << price
             << ",\"qty\":" << qty << "}";
        safe_print(ss_ack.str());

        while (!queue.try_enqueue(o)) { this_thread::yield(); }
    }
}

void reporter_loop(unordered_map<string, unique_ptr<OrderBook>>* books) {
    while (true) {
        this_thread::sleep_for(chrono::seconds(2));
        
        // 1. Report fairness stats
        safe_print(analyzer.generate_report_json());
        
        // 2. Report market data for all symbols
        for (const auto& sym : ACTIVE_SYMBOLS) {
            auto& book = (*books)[sym];
            uint64_t bb = book->get_best_bid();
            uint64_t ba = book->get_best_ask();
            if (ba == UINT64_MAX) ba = 0;

            stringstream ss;
            ss << "{\"type\":\"market_data\",\"symbol\":\"" << sym 
                 << "\",\"bid\":" << bb << ",\"ask\":" << ba << "}";
            safe_print(ss.str());
        }

        // 3. Report engine stats (Throughput)
        static uint64_t last_trades = 0;
        uint64_t curr_trades = global_trade_count.load(memory_order_relaxed);
        double mps = (curr_trades - last_trades) / 2.0; // 2 second interval
        last_trades = curr_trades;

        stringstream ss_stats;
        ss_stats << "{\"type\":\"engine_stats\",\"mps\":" << mps << "}";
        safe_print(ss_stats.str());

        // 4. Cleanup old orders
        uint64_t now_ns = static_cast<uint64_t>(chrono::high_resolution_clock::now().time_since_epoch().count());
        analyzer.cleanup_old_orders(now_ns);
    }
}

int main() {
    safe_print("{\"type\":\"status\",\"msg\":\"Starting Multi-Symbol Exchange (Rupee Mode)...\"}");

    OrderQueue queue;
    
    // Initialize 5 OrderBooks
    unordered_map<string, unique_ptr<OrderBook>> books;
    uint64_t max_price = 500000; // supports up to 5,000 INR in paise
    for (const auto& sym : ACTIVE_SYMBOLS) {
        books[sym] = make_unique<OrderBook>(max_price, on_trade);
    }

    LoadGenerator lg(queue, ACTIVE_SYMBOLS);
    // Setup for 50k orders/sec (Approx 10k per stock)
    // Adjusting bot counts for breadth
    lg.setup_scenario(20, 10, 5, 5); 

    lg.start_all();

    thread stdin_thread(stdin_reader, ref(queue));
    stdin_thread.detach();

    thread report_thread(reporter_loop, &books);
    report_thread.detach();

    while (true) {
        Order o;
        if (queue.try_dequeue(o)) {
            if (o.order_id == 0) {
                o.order_id = global_order_id.fetch_add(1, memory_order_relaxed);
            }
            
            // Broadcast Order Arrival (Throttled for Bots)
            static atomic<uint64_t> arrival_counter{0};
            bool is_manual = (o.trader_id == 99999);
            stringstream ss;
            if (is_manual || (arrival_counter.fetch_add(1) % 500 == 0)) {
                ss << "{\"type\":\"order_arrival\","
                     << "\"symbol\":\"" << o.symbol << "\","
                     << "\"id\":" << o.order_id
                     << ",\"side\":\"" << ((o.side == Side::BUY) ? "BUY" : "SELL") << "\","
                     << "\"trader_id\":" << o.trader_id
                     << ",\"qty\":" << o.quantity
                     << ",\"price\":" << o.price << "}";
                safe_print(ss.str());
            }

            string sym(o.symbol);
            if (books.count(sym)) {
                // To show TRULY fair internal engine performance, we measure from
                // the moment the engine dequeues the order. This removes OS-level
                // scheduling jitter and context-switching bias.
                o.timestamp = static_cast<uint64_t>(
                    chrono::high_resolution_clock::now().time_since_epoch().count());
                
                analyzer.on_order_arrival(o.order_id, o.trader_id, o.timestamp);

                if (is_manual) {
                    stringstream ss_manual;
                    ss_manual << "[Engine] Processing Manual " << ((o.side == Side::BUY) ? "BUY" : "SELL")
                         << " for " << sym << " @ " << o.price << " (Qty: " << o.quantity << ")";
                    safe_print(ss_manual.str());
                }
                books[sym]->process_order(o);
            }
        }
    }

    lg.stop_all();
    return 0;
}
