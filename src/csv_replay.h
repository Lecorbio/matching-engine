#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "matching_engine.h"

struct ReplayStats {
    std::size_t rows_processed = 0;
    std::size_t accepted_actions = 0;
    std::size_t rejected_actions = 0;
    std::size_t cancel_success = 0;
    std::size_t cancel_not_found = 0;
    std::size_t trades_generated = 0;
};

struct ReplayTradeRecord {
    std::uint64_t ts_ns = 0;
    std::uint64_t seq = 0;
    int buy_order_id = 0;
    int sell_order_id = 0;
    PriceTicks price_ticks = 0;
    int quantity = 0;
};

struct ReplayResult {
    ReplayStats stats;
    std::vector<ReplayTradeRecord> trades;
};

bool replay_csv_file(const std::string& csv_path,
                     MatchingEngine& engine,
                     ReplayResult& out_result,
                     std::string& out_error);

bool write_replay_trades_csv(const std::string& csv_path,
                             const std::vector<ReplayTradeRecord>& trades,
                             std::string& out_error);
