#include <cassert>
#include <iostream>

#include "matching_engine.h"

int main() {
    MatchingEngine engine;

    auto t0 = engine.submit({1, Side::BUY, 101.0, 10});
    assert(t0.accepted);
    assert(t0.trades.empty());

    auto t1 = engine.submit({2, Side::SELL, 100.0, 6});
    assert(t1.accepted);
    assert(t1.trades.size() == 1);
    assert(t1.trades[0].quantity == 6);
    assert(t1.trades[0].price == 101.0);
    assert(!engine.bids().empty());

    auto t2 = engine.submit({3, Side::SELL, 101.0, 5});
    assert(t2.accepted);
    assert(t2.trades.size() == 1);
    assert(t2.trades[0].quantity == 4);
    assert(engine.asks().order_count() == 1);
    assert(engine.bids().empty());

    auto t3 = engine.submit({4, Side::BUY, 99.0, 4});
    assert(t3.accepted);
    assert(t3.trades.empty());
    assert(!engine.bids().empty());
    assert(engine.bids().best_price() == 99.0);

    auto t4 = engine.submit({5, Side::BUY, 103.0, 2});
    assert(t4.accepted);
    assert(t4.trades.size() == 1);
    assert(t4.trades[0].quantity == 1);
    assert(t4.trades[0].price == 101.0);
    assert(engine.asks().empty());

    MatchingEngine cancel_engine;
    auto t5 = cancel_engine.submit({6, Side::BUY, 102.0, 3});
    assert(t5.accepted);
    assert(t5.trades.empty());
    auto t6 = cancel_engine.submit({7, Side::BUY, 100.0, 2});
    assert(t6.accepted);
    assert(t6.trades.empty());
    assert(cancel_engine.bids().best_price() == 102.0);

    assert(cancel_engine.cancel(6));
    assert(cancel_engine.bids().best_price() == 100.0);
    assert(!cancel_engine.cancel(6));
    assert(!cancel_engine.cancel(9999));

    MatchingEngine safety_engine;
    auto s0 = safety_engine.submit({100, Side::BUY, 101.0, 5});
    assert(s0.accepted);
    assert(s0.trades.empty());
    assert(safety_engine.has_order(100));
    assert(safety_engine.bids().order_count() == 1);
    assert(safety_engine.asks().empty());

    auto duplicate = safety_engine.submit({100, Side::SELL, 100.0, 2});
    assert(!duplicate.accepted);
    assert(duplicate.trades.empty());
    assert(safety_engine.bids().order_count() == 1);
    assert(safety_engine.asks().empty());

    auto zero_qty = safety_engine.submit({101, Side::BUY, 101.0, 0});
    assert(!zero_qty.accepted);
    assert(zero_qty.trades.empty());
    assert(!safety_engine.has_order(101));

    auto zero_price = safety_engine.submit({102, Side::SELL, 0.0, 2});
    assert(!zero_price.accepted);
    assert(zero_price.trades.empty());
    assert(!safety_engine.has_order(102));

    auto negative_price = safety_engine.submit({103, Side::SELL, -1.0, 2});
    assert(!negative_price.accepted);
    assert(negative_price.trades.empty());
    assert(!safety_engine.has_order(103));

    std::cout << "All matching tests passed.\n";
    return 0;
}
