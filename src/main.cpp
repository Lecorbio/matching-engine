#include <iostream>

#include "matching_engine.h"

int main() {
    MatchingEngine engine;

    const auto first = engine.submit({1, Side::BUY, price_to_ticks(101.5), 10});
    const auto result = engine.submit({2, Side::SELL, price_to_ticks(100.5), 6});

    std::cout << "First order accepted: " << (first.accepted ? "yes" : "no") << '\n';
    std::cout << "Second order accepted: " << (result.accepted ? "yes" : "no") << '\n';
    std::cout << "Executed " << result.trades.size() << " trade(s):\n";
    for (const auto& trade : result.trades) {
        std::cout << "  qty " << trade.quantity
                  << " @ " << ticks_to_price(trade.price_ticks)
                  << " (buy " << trade.buy_order_id
                  << " vs sell " << trade.sell_order_id << ")\n";
    }

    std::cout << "Resting bids: " << engine.bids().order_count()
              << " | resting asks: " << engine.asks().order_count() << '\n';
    return 0;
}
