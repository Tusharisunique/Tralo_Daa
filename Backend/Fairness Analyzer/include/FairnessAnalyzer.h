// Tracks and analyzes latencies and fill rates to ensure market fairness.
#pragma once

#include "Types.h"
#include <string>
#include <unordered_map>
#include <mutex>

struct FairnessTraderStats {
    uint32_t trader_id;
    uint64_t total_orders = 0;
    uint64_t total_matches = 0;
    uint64_t total_latency_ns = 0;
    
    double get_avg_latency_ms() const {
        if (total_matches == 0) return 0.0;
        return (double)total_latency_ns / total_matches / 1000000.0;
    }
    
    double get_fill_rate() const {
        if (total_orders == 0) return 0.0;
        return (double)total_matches / total_orders * 100.0;
    }
};

class FairnessAnalyzer {
private:
    struct SubmissionInfo {
        uint64_t timestamp;
        uint32_t trader_id;
    };
    // submission_time maps order_id -> SubmissionInfo
    std::unordered_map<uint64_t, SubmissionInfo> submission_times;
    std::unordered_map<uint32_t, FairnessTraderStats> stats;
    std::mutex mtx;

public:
    void on_order_arrival(uint64_t order_id, uint32_t trader_id, uint64_t ts) {
        std::lock_guard<std::mutex> lock(mtx);
        submission_times[order_id] = {ts, trader_id};
        stats[trader_id].trader_id = trader_id;
        stats[trader_id].total_orders++;
    }

    uint64_t on_match(uint64_t maker_order_id, uint64_t taker_order_id, uint64_t match_ts) {
        std::lock_guard<std::mutex> lock(mtx);
        
        // Remove maker if exists (to free memory)
        submission_times.erase(maker_order_id);

        uint64_t calculated_latency = 0;

        // Process taker latency and remove from map
        auto it = submission_times.find(taker_order_id);
        if (it != submission_times.end()) {
            uint32_t taker_trader_id = it->second.trader_id;
            calculated_latency = (match_ts > it->second.timestamp) ? (match_ts - it->second.timestamp) : 0;
            stats[taker_trader_id].total_matches++;
            stats[taker_trader_id].total_latency_ns += calculated_latency;
            submission_times.erase(it);
        }
        return calculated_latency;
    }

    void cleanup_old_orders(uint64_t current_ts, uint64_t ttl_ns = 30000000000ULL) { // 30s default
        std::lock_guard<std::mutex> lock(mtx);
        auto it = submission_times.begin();
        while (it != submission_times.end()) {
            if (current_ts > it->second.timestamp + ttl_ns) {
                it = submission_times.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string generate_report_json() {
        std::lock_guard<std::mutex> lock(mtx);
        std::string json = "{\"type\":\"fairness_stats\",\"data\":[";
        bool first = true;
        for (auto const& [id, s] : stats) {
            if (!first) json += ",";
            json += "{\"id\":" + std::to_string(id) + 
                    ",\"avg_lat\":" + std::to_string(s.get_avg_latency_ms()) + 
                    ",\"fill_rate\":" + std::to_string(s.get_fill_rate()) + "}";
            first = false;
        }
        json += "]}";
        return json;
    }
};
