#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "csv_replay.h"

struct TwapConfig {
    Side side = Side::BUY;
    int target_quantity = 0;
    std::size_t slices = 1;
    int first_child_order_id = 1000000000;
};

struct TwapChildExecution {
    int child_index = 0;
    int order_id = 0;
    std::uint64_t scheduled_ts_ns = 0;
    int requested_quantity = 0;
    bool accepted = false;
    RejectReason reject_reason = RejectReason::NONE;
    int filled_quantity = 0;
    std::optional<PriceTicks> average_fill_price_ticks;
};

struct TcaSummary {
    int target_quantity = 0;
    int filled_quantity = 0;
    int unfilled_quantity = 0;
    double fill_rate = 0.0;

    std::optional<PriceTicks> arrival_benchmark_price_ticks;
    std::string arrival_benchmark_name = "UNAVAILABLE";
    std::optional<PriceTicks> average_fill_price_ticks;
    std::optional<double> implementation_shortfall_bps;

    std::uint64_t market_traded_quantity = 0;
    double participation_rate = 0.0;
};

struct TwapBacktestResult {
    ReplayStats replay_stats;
    std::vector<ReplayTradeRecord> market_trades;
    std::vector<TwapChildExecution> child_orders;
    TcaSummary tca;
};

bool run_twap_backtest_csv(const std::string& csv_path,
                           const TwapConfig& config,
                           TwapBacktestResult& out_result,
                           std::string& out_error);
