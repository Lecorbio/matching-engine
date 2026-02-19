#include "execution_backtest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <numeric>
#include <utility>

#include "replay_rows.h"

namespace {

struct AllocationRemainder {
    std::size_t index = 0;
    long double fraction = 0.0L;
    std::uint64_t weight = 0;
};

int planned_twap_slice_quantity(int total_quantity, std::size_t slices, std::size_t slice_index) {
    const int base = total_quantity / static_cast<int>(slices);
    const int remainder = total_quantity % static_cast<int>(slices);
    return base + (slice_index < static_cast<std::size_t>(remainder) ? 1 : 0);
}

std::vector<std::uint64_t> build_even_schedule(const std::vector<ReplayRow>& rows, std::size_t slices) {
    std::vector<std::uint64_t> schedule;
    schedule.reserve(slices);

    const std::uint64_t start_ts = rows.front().ts_ns;
    const std::uint64_t end_ts = rows.back().ts_ns;
    const std::uint64_t span = end_ts - start_ts;

    if (slices == 1 || span == 0) {
        for (std::size_t i = 0; i < slices; ++i) {
            schedule.push_back(start_ts);
        }
        return schedule;
    }

    for (std::size_t i = 0; i < slices; ++i) {
        const std::uint64_t offset = (span * i) / (slices - 1);
        schedule.push_back(start_ts + offset);
    }
    return schedule;
}

std::optional<PriceTicks> capture_arrival_benchmark(const MatchingEngine& engine,
                                                    Side side,
                                                    std::string& out_name) {
    const TopOfBook top = engine.top_of_book();
    if (top.best_bid.has_value() && top.best_ask.has_value()) {
        out_name = "MID";
        return (top.best_bid->price_ticks + top.best_ask->price_ticks) / 2;
    }

    if (side == Side::BUY && top.best_ask.has_value()) {
        out_name = "BEST_ASK";
        return top.best_ask->price_ticks;
    }

    if (side == Side::SELL && top.best_bid.has_value()) {
        out_name = "BEST_BID";
        return top.best_bid->price_ticks;
    }

    out_name = "UNAVAILABLE";
    return std::nullopt;
}

void append_market_trades(const ReplayRow& row,
                          const std::vector<Trade>& trades,
                          BacktestResult& backtest,
                          std::uint64_t& market_traded_quantity) {
    backtest.replay_stats.trades_generated += trades.size();
    for (const auto& trade : trades) {
        backtest.market_trades.push_back(
            {row.ts_ns, row.seq, trade.buy_order_id, trade.sell_order_id, trade.price_ticks, trade.quantity});
        market_traded_quantity += static_cast<std::uint64_t>(trade.quantity);
    }
}

int fill_quantity_from_child_trades(const std::vector<Trade>& trades, Side side, int child_order_id) {
    int filled = 0;
    for (const auto& trade : trades) {
        const bool involved =
            side == Side::BUY ? (trade.buy_order_id == child_order_id)
                              : (trade.sell_order_id == child_order_id);
        if (involved) {
            filled += trade.quantity;
        }
    }
    return filled;
}

std::optional<PriceTicks> average_fill_price_from_child_trades(const std::vector<Trade>& trades,
                                                               Side side,
                                                               int child_order_id) {
    int filled_quantity = 0;
    long double notional_ticks = 0.0L;

    for (const auto& trade : trades) {
        const bool involved =
            side == Side::BUY ? (trade.buy_order_id == child_order_id)
                              : (trade.sell_order_id == child_order_id);
        if (!involved) {
            continue;
        }

        filled_quantity += trade.quantity;
        notional_ticks +=
            static_cast<long double>(trade.price_ticks) * static_cast<long double>(trade.quantity);
    }

    if (filled_quantity == 0) {
        return std::nullopt;
    }

    return static_cast<PriceTicks>(std::llround(notional_ticks / filled_quantity));
}

bool validate_config(const BacktestConfig& config, std::string& out_error) {
    if (config.target_quantity <= 0) {
        out_error = "target_quantity must be positive";
        return false;
    }

    if (config.slices == 0) {
        out_error = "slices must be at least 1";
        return false;
    }

    if (config.slices > static_cast<std::size_t>(config.target_quantity)) {
        out_error = "slices must be less than or equal to target_quantity";
        return false;
    }

    if (config.first_child_order_id <= 0) {
        out_error = "first_child_order_id must be positive";
        return false;
    }

    const long long max_order_id =
        static_cast<long long>(config.first_child_order_id) + static_cast<long long>(config.slices) - 1;
    if (max_order_id > static_cast<long long>(std::numeric_limits<int>::max())) {
        out_error = "child order id range exceeds int max";
        return false;
    }

    return true;
}

std::size_t bucket_index_for_ts(std::uint64_t ts_ns,
                                std::uint64_t start_ts,
                                std::uint64_t end_ts,
                                std::size_t buckets) {
    if (buckets <= 1 || end_ts <= start_ts) {
        return 0;
    }

    std::uint64_t bounded_ts = ts_ns;
    if (bounded_ts < start_ts) {
        bounded_ts = start_ts;
    }
    if (bounded_ts > end_ts) {
        bounded_ts = end_ts;
    }

    const std::uint64_t span = end_ts - start_ts;
    const std::uint64_t offset = bounded_ts - start_ts;
    const std::uint64_t denominator = span + 1;
    const std::uint64_t numerator = offset * buckets;
    std::size_t index = static_cast<std::size_t>(numerator / denominator);
    if (index >= buckets) {
        index = buckets - 1;
    }
    return index;
}

std::vector<std::uint64_t> build_market_volume_profile_by_bucket(const std::vector<ReplayRow>& rows,
                                                                  std::size_t buckets) {
    std::vector<std::uint64_t> bucket_volume(buckets, 0);
    if (rows.empty()) {
        return bucket_volume;
    }

    const std::uint64_t start_ts = rows.front().ts_ns;
    const std::uint64_t end_ts = rows.back().ts_ns;

    MatchingEngine market_engine;
    for (const auto& row : rows) {
        std::vector<Trade> trades;

        if (row.action == ReplayAction::NEW) {
            SubmitResult result = market_engine.submit(
                {row.order_id, row.side, row.price_ticks, row.quantity, row.tif, row.type});
            trades = std::move(result.trades);
        } else if (row.action == ReplayAction::CANCEL) {
            market_engine.cancel(row.order_id);
        } else {
            SubmitResult result =
                market_engine.replace(row.order_id, row.new_price_ticks, row.new_quantity);
            trades = std::move(result.trades);
        }

        if (trades.empty()) {
            continue;
        }

        const std::size_t bucket_idx = bucket_index_for_ts(row.ts_ns, start_ts, end_ts, buckets);
        for (const auto& trade : trades) {
            bucket_volume[bucket_idx] += static_cast<std::uint64_t>(trade.quantity);
        }
    }

    return bucket_volume;
}

std::vector<int> allocate_vwap_quantities(int target_quantity,
                                          const std::vector<std::uint64_t>& bucket_volume) {
    std::vector<int> quantities(bucket_volume.size(), 0);

    const std::uint64_t total_volume =
        std::accumulate(bucket_volume.begin(), bucket_volume.end(), static_cast<std::uint64_t>(0));

    if (total_volume == 0) {
        for (std::size_t i = 0; i < quantities.size(); ++i) {
            quantities[i] = planned_twap_slice_quantity(target_quantity, quantities.size(), i);
        }
        return quantities;
    }

    int assigned = 0;
    std::vector<AllocationRemainder> remainders;
    remainders.reserve(bucket_volume.size());

    for (std::size_t i = 0; i < bucket_volume.size(); ++i) {
        const long double exact_quantity =
            (static_cast<long double>(target_quantity) * static_cast<long double>(bucket_volume[i])) /
            static_cast<long double>(total_volume);

        const int base_quantity = static_cast<int>(std::floor(exact_quantity));
        quantities[i] = base_quantity;
        assigned += base_quantity;

        remainders.push_back(
            {i, exact_quantity - static_cast<long double>(base_quantity), bucket_volume[i]});
    }

    int remainder = target_quantity - assigned;
    std::sort(remainders.begin(), remainders.end(), [](const AllocationRemainder& lhs,
                                                       const AllocationRemainder& rhs) {
        if (lhs.fraction != rhs.fraction) {
            return lhs.fraction > rhs.fraction;
        }
        if (lhs.weight != rhs.weight) {
            return lhs.weight > rhs.weight;
        }
        return lhs.index < rhs.index;
    });

    for (int i = 0; i < remainder; ++i) {
        quantities[remainders[static_cast<std::size_t>(i)].index] += 1;
    }

    return quantities;
}

std::vector<int> build_slice_quantities(const std::vector<ReplayRow>& rows, const BacktestConfig& config) {
    std::vector<int> quantities(config.slices, 0);

    if (config.strategy == ExecutionStrategy::TWAP) {
        for (std::size_t i = 0; i < quantities.size(); ++i) {
            quantities[i] = planned_twap_slice_quantity(config.target_quantity, config.slices, i);
        }
        return quantities;
    }

    const std::vector<std::uint64_t> volume_profile =
        build_market_volume_profile_by_bucket(rows, config.slices);
    return allocate_vwap_quantities(config.target_quantity, volume_profile);
}

void update_tca_summary(const BacktestConfig& config,
                       int total_filled,
                       long double total_notional_ticks,
                       std::uint64_t market_traded_quantity,
                       BacktestResult& out_result) {
    out_result.tca.filled_quantity = total_filled;
    out_result.tca.unfilled_quantity = config.target_quantity - total_filled;
    out_result.tca.fill_rate =
        static_cast<double>(total_filled) / static_cast<double>(config.target_quantity);

    if (total_filled > 0) {
        out_result.tca.average_fill_price_ticks =
            static_cast<PriceTicks>(std::llround(total_notional_ticks / total_filled));
    }

    if (out_result.tca.average_fill_price_ticks.has_value() &&
        out_result.tca.arrival_benchmark_price_ticks.has_value()) {
        const long double average_fill =
            static_cast<long double>(out_result.tca.average_fill_price_ticks.value());
        const long double benchmark =
            static_cast<long double>(out_result.tca.arrival_benchmark_price_ticks.value());

        if (benchmark > 0) {
            long double shortfall = 0.0L;
            if (config.side == Side::BUY) {
                shortfall = (average_fill - benchmark) / benchmark;
            } else {
                shortfall = (benchmark - average_fill) / benchmark;
            }
            out_result.tca.implementation_shortfall_bps = static_cast<double>(shortfall * 10000.0L);
        }
    }

    out_result.tca.market_traded_quantity = market_traded_quantity;
    if (market_traded_quantity > 0) {
        out_result.tca.participation_rate =
            static_cast<double>(total_filled) / static_cast<double>(market_traded_quantity);
    }
}

}  // namespace

bool run_execution_backtest_csv(const std::string& csv_path,
                                const BacktestConfig& config,
                                BacktestResult& out_result,
                                std::string& out_error) {
    out_result = BacktestResult{};
    out_result.tca.target_quantity = config.target_quantity;

    if (!validate_config(config, out_error)) {
        return false;
    }

    std::vector<ReplayRow> rows;
    if (!parse_replay_csv_rows(csv_path, rows, out_error)) {
        return false;
    }
    if (rows.empty()) {
        out_error = "CSV has no replay rows";
        return false;
    }
    sort_replay_rows(rows);

    const std::vector<std::uint64_t> schedule = build_even_schedule(rows, config.slices);
    const std::vector<int> slice_quantities = build_slice_quantities(rows, config);

    MatchingEngine engine;
    out_result.child_orders.reserve(config.slices);

    int total_filled = 0;
    long double total_notional_ticks = 0.0L;
    std::uint64_t market_traded_quantity = 0;

    bool benchmark_attempted = false;

    std::size_t next_slice_index = 0;
    auto send_due_slices = [&](std::uint64_t now_ts_ns) {
        while (next_slice_index < schedule.size() && schedule[next_slice_index] <= now_ts_ns) {
            const int request_qty = slice_quantities[next_slice_index];
            const int child_order_id = config.first_child_order_id + static_cast<int>(next_slice_index);

            ChildExecution child;
            child.child_index = static_cast<int>(next_slice_index) + 1;
            child.order_id = child_order_id;
            child.scheduled_ts_ns = schedule[next_slice_index];
            child.requested_quantity = request_qty;

            if (!benchmark_attempted) {
                benchmark_attempted = true;
                std::string benchmark_name;
                const std::optional<PriceTicks> benchmark =
                    capture_arrival_benchmark(engine, config.side, benchmark_name);
                if (benchmark.has_value()) {
                    out_result.tca.arrival_benchmark_price_ticks = benchmark;
                    out_result.tca.arrival_benchmark_name = benchmark_name;
                }
            }

            if (request_qty <= 0) {
                child.skipped = true;
                child.accepted = true;
                child.reject_reason = RejectReason::NONE;
                out_result.child_orders.push_back(child);
                ++next_slice_index;
                continue;
            }

            SubmitResult result = engine.submit(
                {child_order_id, config.side, 0, request_qty, TimeInForce::IOC, OrderType::MARKET});
            child.accepted = result.accepted;
            child.reject_reason = result.reject_reason;

            child.filled_quantity = fill_quantity_from_child_trades(result.trades, config.side, child_order_id);
            child.average_fill_price_ticks =
                average_fill_price_from_child_trades(result.trades, config.side, child_order_id);

            if (child.filled_quantity > 0) {
                total_filled += child.filled_quantity;
                for (const auto& trade : result.trades) {
                    const bool involved =
                        config.side == Side::BUY ? (trade.buy_order_id == child_order_id)
                                                 : (trade.sell_order_id == child_order_id);
                    if (involved) {
                        total_notional_ticks += static_cast<long double>(trade.price_ticks) *
                                                static_cast<long double>(trade.quantity);
                    }
                }
            }

            out_result.child_orders.push_back(child);
            ++next_slice_index;
        }
    };

    for (const auto& row : rows) {
        ++out_result.replay_stats.rows_processed;

        if (row.action == ReplayAction::NEW) {
            SubmitResult result = engine.submit(
                {row.order_id, row.side, row.price_ticks, row.quantity, row.tif, row.type});
            if (result.accepted) {
                ++out_result.replay_stats.accepted_actions;
            } else {
                ++out_result.replay_stats.rejected_actions;
            }
            append_market_trades(row, result.trades, out_result, market_traded_quantity);
        } else if (row.action == ReplayAction::CANCEL) {
            if (engine.cancel(row.order_id)) {
                ++out_result.replay_stats.accepted_actions;
                ++out_result.replay_stats.cancel_success;
            } else {
                ++out_result.replay_stats.rejected_actions;
                ++out_result.replay_stats.cancel_not_found;
            }
        } else {
            SubmitResult result = engine.replace(row.order_id, row.new_price_ticks, row.new_quantity);
            if (result.accepted) {
                ++out_result.replay_stats.accepted_actions;
            } else {
                ++out_result.replay_stats.rejected_actions;
            }
            append_market_trades(row, result.trades, out_result, market_traded_quantity);
        }

        send_due_slices(row.ts_ns);
    }

    while (next_slice_index < schedule.size()) {
        send_due_slices(schedule[next_slice_index]);
    }

    update_tca_summary(config, total_filled, total_notional_ticks, market_traded_quantity, out_result);
    return true;
}

bool run_twap_backtest_csv(const std::string& csv_path,
                           const TwapConfig& config,
                           TwapBacktestResult& out_result,
                           std::string& out_error) {
    BacktestConfig twap_config = config;
    twap_config.strategy = ExecutionStrategy::TWAP;
    return run_execution_backtest_csv(csv_path, twap_config, out_result, out_error);
}

bool run_vwap_backtest_csv(const std::string& csv_path,
                           const BacktestConfig& config,
                           BacktestResult& out_result,
                           std::string& out_error) {
    BacktestConfig vwap_config = config;
    vwap_config.strategy = ExecutionStrategy::VWAP;
    return run_execution_backtest_csv(csv_path, vwap_config, out_result, out_error);
}
