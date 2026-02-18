#include <cassert>
#include <cmath>
#include <string>

#include "execution_backtest.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
#endif

namespace {

std::string data_path(const std::string& filename) {
    return std::string(TEST_DATA_DIR) + "/" + filename;
}

bool nearly_equal(double lhs, double rhs, double tolerance) {
    return std::fabs(lhs - rhs) <= tolerance;
}

}  // namespace

int main() {
    TwapBacktestResult result;
    std::string error;

    TwapConfig config;
    config.side = Side::BUY;
    config.target_quantity = 6;
    config.slices = 3;
    const bool ok = run_twap_backtest_csv(data_path("backtest_twap_basic.csv"), config, result, error);
    assert(ok);

    assert(result.replay_stats.rows_processed == 7);
    assert(result.replay_stats.accepted_actions == 7);
    assert(result.replay_stats.rejected_actions == 0);
    assert(result.replay_stats.cancel_success == 1);
    assert(result.replay_stats.cancel_not_found == 0);
    assert(result.replay_stats.trades_generated == 1);

    assert(result.child_orders.size() == 3);
    for (std::size_t i = 0; i < result.child_orders.size(); ++i) {
        const auto& child = result.child_orders[i];
        assert(child.child_index == static_cast<int>(i + 1));
        assert(child.accepted);
        assert(child.reject_reason == RejectReason::NONE);
        assert(child.requested_quantity == 2);
        assert(child.filled_quantity == 2);
        assert(child.average_fill_price_ticks.has_value());
    }

    assert(result.child_orders[0].average_fill_price_ticks.value() == price_to_ticks(100.0));
    assert(result.child_orders[1].average_fill_price_ticks.value() == price_to_ticks(100.0));
    assert(result.child_orders[2].average_fill_price_ticks.value() == price_to_ticks(100.2));

    assert(result.tca.target_quantity == 6);
    assert(result.tca.filled_quantity == 6);
    assert(result.tca.unfilled_quantity == 0);
    assert(nearly_equal(result.tca.fill_rate, 1.0, 1e-9));

    assert(result.tca.arrival_benchmark_price_ticks.has_value());
    assert(result.tca.arrival_benchmark_price_ticks.value() == price_to_ticks(100.0));
    assert(result.tca.arrival_benchmark_name == "BEST_ASK");

    assert(result.tca.average_fill_price_ticks.has_value());
    assert(result.tca.average_fill_price_ticks.value() == price_to_ticks(100.0667));

    assert(result.tca.implementation_shortfall_bps.has_value());
    assert(nearly_equal(result.tca.implementation_shortfall_bps.value(), 6.67, 0.01));

    assert(result.tca.market_traded_quantity == 1);
    assert(nearly_equal(result.tca.participation_rate, 6.0, 1e-9));

    TwapBacktestResult invalid_result;
    TwapConfig invalid_config;
    invalid_config.side = Side::BUY;
    invalid_config.target_quantity = 2;
    invalid_config.slices = 3;
    const bool invalid_ok =
        run_twap_backtest_csv(data_path("backtest_twap_basic.csv"), invalid_config, invalid_result, error);
    assert(!invalid_ok);
    assert(error.find("slices") != std::string::npos);

    return 0;
}
