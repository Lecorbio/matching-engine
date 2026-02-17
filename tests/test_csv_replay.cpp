#include <cassert>
#include <string>

#include "csv_replay.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
#endif

namespace {

std::string data_path(const std::string& filename) {
    return std::string(TEST_DATA_DIR) + "/" + filename;
}

void assert_trade_matches(const ReplayTradeRecord& trade,
                          std::uint64_t ts_ns,
                          std::uint64_t seq,
                          int buy_order_id,
                          int sell_order_id,
                          PriceTicks price_ticks,
                          int quantity) {
    assert(trade.ts_ns == ts_ns);
    assert(trade.seq == seq);
    assert(trade.buy_order_id == buy_order_id);
    assert(trade.sell_order_id == sell_order_id);
    assert(trade.price_ticks == price_ticks);
    assert(trade.quantity == quantity);
}

}  // namespace

int main() {
    const std::string replay_path = data_path("replay_basic.csv");

    MatchingEngine first_engine;
    ReplayResult first_result;
    std::string error;
    const bool first_ok = replay_csv_file(replay_path, first_engine, first_result, error);
    assert(first_ok);

    MatchingEngine second_engine;
    ReplayResult second_result;
    const bool second_ok = replay_csv_file(replay_path, second_engine, second_result, error);
    assert(second_ok);

    assert(first_result.stats.rows_processed == 9);
    assert(first_result.stats.accepted_actions == 7);
    assert(first_result.stats.rejected_actions == 2);
    assert(first_result.stats.cancel_success == 1);
    assert(first_result.stats.cancel_not_found == 0);
    assert(first_result.stats.trades_generated == 3);
    assert(first_result.trades.size() == 3);

    assert_trade_matches(first_result.trades[0], 100, 2, 1, 2, price_to_ticks(100.0), 2);
    assert_trade_matches(first_result.trades[1], 106, 1, 1, 3, price_to_ticks(101.0), 3);
    assert_trade_matches(first_result.trades[2], 109, 1, 6, 3, price_to_ticks(101.0), 1);

    assert(first_engine.last_seq_num() == 9);
    const TopOfBook final_top = first_engine.top_of_book();
    assert(!final_top.best_bid.has_value());
    assert(!final_top.best_ask.has_value());

    assert(first_result.stats.rows_processed == second_result.stats.rows_processed);
    assert(first_result.stats.accepted_actions == second_result.stats.accepted_actions);
    assert(first_result.stats.rejected_actions == second_result.stats.rejected_actions);
    assert(first_result.stats.cancel_success == second_result.stats.cancel_success);
    assert(first_result.stats.cancel_not_found == second_result.stats.cancel_not_found);
    assert(first_result.stats.trades_generated == second_result.stats.trades_generated);
    assert(first_result.trades.size() == second_result.trades.size());
    for (std::size_t i = 0; i < first_result.trades.size(); ++i) {
        const auto& lhs = first_result.trades[i];
        const auto& rhs = second_result.trades[i];
        assert(lhs.ts_ns == rhs.ts_ns);
        assert(lhs.seq == rhs.seq);
        assert(lhs.buy_order_id == rhs.buy_order_id);
        assert(lhs.sell_order_id == rhs.sell_order_id);
        assert(lhs.price_ticks == rhs.price_ticks);
        assert(lhs.quantity == rhs.quantity);
    }

    MatchingEngine invalid_engine;
    ReplayResult invalid_result;
    const bool invalid_ok =
        replay_csv_file(data_path("replay_invalid.csv"), invalid_engine, invalid_result, error);
    assert(!invalid_ok);
    assert(error.find("line 2") != std::string::npos);

    return 0;
}
