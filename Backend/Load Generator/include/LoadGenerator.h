#pragma once

#include "OrderQueue.h"
#include "Trader.h"
#include "TraderProfiles.h"
#include <atomic>
#include <memory>
#include <vector>

class LoadGenerator {
private:
  OrderQueue &queue;
  std::vector<std::unique_ptr<Trader>> traders;

  std::atomic<uint64_t> global_generated{0};
  std::atomic<uint64_t> global_dropped{0};

  std::atomic<bool> monitoring{false};
  std::thread monitor_thread;

  std::vector<std::string> symbols;
  void monitor_loop();

public:
  LoadGenerator(OrderQueue &q, const std::vector<std::string>& symbols);
  ~LoadGenerator();

  void setup_scenario(int num_retail, int num_hft, int num_mm, int num_sniper);
  void start_all();
  void stop_all();
};
