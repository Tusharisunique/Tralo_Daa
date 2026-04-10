#include "LoadGenerator.h"
#include <iomanip>
#include <iostream>

using namespace std;

LoadGenerator::LoadGenerator(OrderQueue &q, const vector<string>& syms) : queue(q), symbols(syms) {}

LoadGenerator::~LoadGenerator() { stop_all(); }

void LoadGenerator::setup_scenario(int num_retail, int num_hft, int num_mm,
                                   int num_sniper) {
  int total_traders = num_retail + num_hft + num_mm + num_sniper;
  uint32_t current_id = 1;

  for (int i = 0; i < num_retail; ++i) {
    traders.push_back(make_unique<RetailTrader>(current_id++, queue,
                                               global_generated, global_dropped, symbols));
  }
  for (int i = 0; i < num_hft; ++i) {
    traders.push_back(
        make_unique<HFTTrader>(current_id++, queue, global_generated, global_dropped, symbols));
  }
  for (int i = 0; i < num_mm; ++i) {
    traders.push_back(make_unique<MarketMaker>(current_id++, queue,
                                              global_generated, global_dropped, symbols));
  }
  for (int i = 0; i < num_sniper; ++i) {
    traders.push_back(make_unique<SniperTrader>(current_id++, queue,
                                               global_generated, global_dropped, symbols));
  }

  cout << "[C++] Scenario configured with " << total_traders << " total traders.\n";
  cout.flush();
}

void LoadGenerator::start_all() {
  for (auto &t : traders) {
    t->start();
  }
  monitoring = true;
  monitor_thread = thread(&LoadGenerator::monitor_loop, this);
}

void LoadGenerator::stop_all() {
  monitoring = false;
  if (monitor_thread.joinable()) {
    monitor_thread.join();
  }
  for (auto &t : traders) {
    t->stop();
  }
}

void LoadGenerator::monitor_loop() {
  uint64_t last_gen = 0;
  uint64_t last_drop = 0;
  auto last_time = chrono::high_resolution_clock::now();

  while (monitoring.load(std::memory_order_relaxed)) {
    this_thread::sleep_for(chrono::seconds(5));

    auto curr_time = chrono::high_resolution_clock::now();
    uint64_t curr_gen = global_generated.load(std::memory_order_relaxed);
    uint64_t curr_drop = global_dropped.load(std::memory_order_relaxed);

    double elapsed =
        chrono::duration_cast<chrono::milliseconds>(curr_time - last_time)
            .count();
    double gen_rate = (curr_gen - last_gen) * 1000.0 / elapsed;
    double drop_rate = (curr_drop - last_drop) * 1000.0 / elapsed;

    // Use a specific message type so Python can ignore it if it wants, 
    // or log it for diagnostics.
    cout << "{\"type\":\"load_stats\",\"gen_rate\":" << fixed << setprecision(1) << gen_rate 
         << ",\"drop_rate\":" << drop_rate << "}\n";
    cout.flush();

    last_gen = curr_gen;
    last_drop = curr_drop;
    last_time = curr_time;
  }
}
