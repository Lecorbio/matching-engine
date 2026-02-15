#include <cassert>
#include <iostream>

#include "matching_engine.h"

namespace {

PriceTicks px(double price) {
    return price_to_ticks(price);
}

}  

int main() {
    MatchingEngine engine;

    auto t0 = engine.submit({1, Side::BUY, px(101.0), 10});
    assert(t0.accepted);
    assert(t0.reject_reason == RejectReason::NONE);
    assert(t0.trades.empty());

    auto t1 = engine.submit({2, Side::SELL, px(100.0), 6});
    assert(t1.accepted);
    assert(t1.reject_reason == RejectReason::NONE);
    assert(t1.trades.size() == 1);
    assert(t1.trades[0].quantity == 6);
    assert(t1.trades[0].price_ticks == px(101.0));
    assert(!engine.bids().empty());

    auto t2 = engine.submit({3, Side::SELL, px(101.0), 5});
    assert(t2.accepted);
    assert(t2.reject_reason == RejectReason::NONE);
    assert(t2.trades.size() == 1);
    assert(t2.trades[0].quantity == 4);
    assert(engine.asks().order_count() == 1);
    assert(engine.bids().empty());

    auto t3 = engine.submit({4, Side::BUY, px(99.0), 4});
    assert(t3.accepted);
    assert(t3.reject_reason == RejectReason::NONE);
    assert(t3.trades.empty());
    assert(!engine.bids().empty());
    assert(engine.bids().best_price_ticks() == px(99.0));

    auto t4 = engine.submit({5, Side::BUY, px(103.0), 2});
    assert(t4.accepted);
    assert(t4.reject_reason == RejectReason::NONE);
    assert(t4.trades.size() == 1);
    assert(t4.trades[0].quantity == 1);
    assert(t4.trades[0].price_ticks == px(101.0));
    assert(engine.asks().empty());

    MatchingEngine cancel_engine;
    auto t5 = cancel_engine.submit({6, Side::BUY, px(102.0), 3});
    assert(t5.accepted);
    assert(t5.reject_reason == RejectReason::NONE);
    assert(t5.trades.empty());
    auto t6 = cancel_engine.submit({7, Side::BUY, px(100.0), 2});
    assert(t6.accepted);
    assert(t6.reject_reason == RejectReason::NONE);
    assert(t6.trades.empty());
    assert(cancel_engine.bids().best_price_ticks() == px(102.0));

    assert(cancel_engine.cancel(6));
    assert(cancel_engine.bids().best_price_ticks() == px(100.0));
    assert(!cancel_engine.cancel(6));
    assert(!cancel_engine.cancel(9999));

    MatchingEngine safety_engine;
    auto s0 = safety_engine.submit({100, Side::BUY, px(101.0), 5});
    assert(s0.accepted);
    assert(s0.reject_reason == RejectReason::NONE);
    assert(s0.trades.empty());
    assert(safety_engine.has_order(100));
    assert(safety_engine.bids().order_count() == 1);
    assert(safety_engine.asks().empty());

    auto duplicate = safety_engine.submit({100, Side::SELL, px(100.0), 2});
    assert(!duplicate.accepted);
    assert(duplicate.reject_reason == RejectReason::DUPLICATE_ORDER_ID);
    assert(duplicate.trades.empty());
    assert(safety_engine.bids().order_count() == 1);
    assert(safety_engine.asks().empty());

    auto zero_qty = safety_engine.submit({101, Side::BUY, px(101.0), 0});
    assert(!zero_qty.accepted);
    assert(zero_qty.reject_reason == RejectReason::INVALID_QUANTITY);
    assert(zero_qty.trades.empty());
    assert(!safety_engine.has_order(101));

    auto zero_price = safety_engine.submit({102, Side::SELL, 0, 2});
    assert(!zero_price.accepted);
    assert(zero_price.reject_reason == RejectReason::INVALID_PRICE);
    assert(zero_price.trades.empty());
    assert(!safety_engine.has_order(102));

    auto negative_price = safety_engine.submit({103, Side::SELL, px(-1.0), 2});
    assert(!negative_price.accepted);
    assert(negative_price.reject_reason == RejectReason::INVALID_PRICE);
    assert(negative_price.trades.empty());
    assert(!safety_engine.has_order(103));

    MatchingEngine tif_engine;
    auto ioc_no_cross = tif_engine.submit({200, Side::BUY, px(99.0), 5, TimeInForce::IOC});
    assert(ioc_no_cross.accepted);
    assert(ioc_no_cross.reject_reason == RejectReason::NONE);
    assert(ioc_no_cross.trades.empty());
    assert(tif_engine.bids().empty());
    assert(!tif_engine.has_order(200));

    auto resting_sell = tif_engine.submit({201, Side::SELL, px(100.0), 3, TimeInForce::GTC});
    assert(resting_sell.accepted);
    assert(resting_sell.trades.empty());
    assert(tif_engine.asks().order_count() == 1);

    auto ioc_partial = tif_engine.submit({202, Side::BUY, px(101.0), 5, TimeInForce::IOC});
    assert(ioc_partial.accepted);
    assert(ioc_partial.reject_reason == RejectReason::NONE);
    assert(ioc_partial.trades.size() == 1);
    assert(ioc_partial.trades[0].quantity == 3);
    assert(ioc_partial.trades[0].price_ticks == px(100.0));
    assert(tif_engine.asks().empty());
    assert(tif_engine.bids().empty());
    assert(!tif_engine.has_order(202));

    MatchingEngine market_engine;
    auto market_no_liquidity =
        market_engine.submit({300, Side::BUY, 0, 3, TimeInForce::IOC, OrderType::MARKET});
    assert(!market_no_liquidity.accepted);
    assert(market_no_liquidity.reject_reason == RejectReason::NO_LIQUIDITY);
    assert(market_no_liquidity.trades.empty());

    auto m0 = market_engine.submit({301, Side::SELL, px(100.0), 2});
    assert(m0.accepted);
    auto m1 = market_engine.submit({302, Side::SELL, px(101.0), 3});
    assert(m1.accepted);
    assert(market_engine.asks().order_count() == 2);

    auto market_buy =
        market_engine.submit({303, Side::BUY, px(-5.0), 7, TimeInForce::GTC, OrderType::MARKET});
    assert(market_buy.accepted);
    assert(market_buy.reject_reason == RejectReason::NONE);
    assert(market_buy.trades.size() == 2);
    assert(market_buy.trades[0].quantity == 2);
    assert(market_buy.trades[0].price_ticks == px(100.0));
    assert(market_buy.trades[1].quantity == 3);
    assert(market_buy.trades[1].price_ticks == px(101.0));
    assert(market_engine.asks().empty());
    assert(market_engine.bids().empty());
    assert(!market_engine.has_order(303));

    MatchingEngine market_sell_engine;
    auto m2 = market_sell_engine.submit({400, Side::BUY, px(100.0), 2});
    assert(m2.accepted);
    auto m3 = market_sell_engine.submit({401, Side::BUY, px(99.0), 4});
    assert(m3.accepted);

    auto market_sell =
        market_sell_engine.submit({402,
                                   Side::SELL,
                                   px(1000.0),
                                   3,
                                   TimeInForce::IOC,
                                   OrderType::MARKET});
    assert(market_sell.accepted);
    assert(market_sell.reject_reason == RejectReason::NONE);
    assert(market_sell.trades.size() == 2);
    assert(market_sell.trades[0].quantity == 2);
    assert(market_sell.trades[0].price_ticks == px(100.0));
    assert(market_sell.trades[1].quantity == 1);
    assert(market_sell.trades[1].price_ticks == px(99.0));
    assert(market_sell_engine.asks().empty());
    assert(market_sell_engine.bids().order_count() == 1);
    assert(!market_sell_engine.has_order(402));

    std::cout << "All matching tests passed.\n";
    return 0;
}
