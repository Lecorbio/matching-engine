#include "execution_backtest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "replay_rows.h"

namespace {

int planned_slice_quantity(int total_quantity, std::size_t slices, std::size_t slice_index) {
    const int base = total_quantity / static_cast<int>(slices);
    const int remainder = total_quantity % static_cast<int>(slices);
    return base + (slice_index < static_cast<std::size_t>(remainder) ? 1 : 0);
}

std::vector<std::uint64_t> build_twap_schedule(const std::vector<ReplayRow>& rows, std::size_t slices) {
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
                          TwapBacktestResult& backtest,
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

bool validate_config(const TwapConfig& config, std::string& out_error) {
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

}  // namespace

bool run_twap_backtest_csv(const std::string& csv_path,
                           const TwapConfig& config,
                           TwapBacktestResult& out_result,
                           std::string& out_error) {
    out_result = TwapBacktestResult{};
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

    const std::vector<std::uint64_t> schedule = build_twap_schedule(rows, config.slices);

    MatchingEngine engine;
    out_result.child_orders.reserve(config.slices);

    int total_filled = 0;
    long double total_notional_ticks = 0.0L;
    std::uint64_t market_traded_quantity = 0;

    std::size_t next_slice_index = 0;
    auto send_due_slices = [&](std::uint64_t now_ts_ns) {
        while (next_slice_index < schedule.size() && schedule[next_slice_index] <= now_ts_ns) {
            const int request_qty =
                planned_slice_quantity(config.target_quantity, config.slices, next_slice_index);
            const int child_order_id = config.first_child_order_id + static_cast<int>(next_slice_index);

            TwapChildExecution child;
            child.child_index = static_cast<int>(next_slice_index) + 1;
            child.order_id = child_order_id;
            child.scheduled_ts_ns = schedule[next_slice_index];
            child.requested_quantity = request_qty;

            if (!out_result.tca.arrival_benchmark_price_ticks.has_value()) {
                std::string benchmark_name;
                const std::optional<PriceTicks> benchmark =
                    capture_arrival_benchmark(engine, config.side, benchmark_name);
                if (benchmark.has_value()) {
                    out_result.tca.arrival_benchmark_price_ticks = benchmark;
                    out_result.tca.arrival_benchmark_name = benchmark_name;
                }
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

                if (!out_result.tca.arrival_benchmark_price_ticks.has_value()) {
                    out_result.tca.arrival_benchmark_price_ticks = child.average_fill_price_ticks;
                    out_result.tca.arrival_benchmark_name = "FIRST_FILL";
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

    return true;
}
