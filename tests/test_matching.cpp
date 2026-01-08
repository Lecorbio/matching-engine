#include <cassert>
#include <iostream>

#include "matching_engine.h"

int main() {
    MatchingEngine engine;

    auto t0 = engine.submit({1, Side::Buy, 101.0, 10});
    assert(t0.empty());

    auto t1 = engine.submit({2, Side::Sell, 100.0, 6});
    assert(t1.size() == 1);
    assert(t1[0].quantity == 6);
    assert(t1[0].price == 101.0);
    assert(!engine.bids().empty());

    auto t2 = engine.submit({3, Side::Sell, 101.0, 5});
    assert(t2.size() == 1);
    assert(t2[0].quantity == 4);
    assert(engine.asks().order_count() == 1);
    assert(engine.bids().empty());

    auto t3 = engine.submit({4, Side::Buy, 99.0, 4});
    assert(t3.empty());
    assert(!engine.bids().empty());
    assert(engine.bids().best_price() == 99.0);

    auto t4 = engine.submit({5, Side::Buy, 103.0, 2});
    assert(t4.size() == 1);
    assert(t4[0].quantity == 1);
    assert(t4[0].price == 101.0);
    assert(engine.asks().empty());

    std::cout << "All matching tests passed.\n";
    return 0;
}
