#include <iostream>

#include "matching_engine.h"

int main() {
    MatchingEngine engine;

    engine.submit({1, Side::BUY, 101.5, 10});
    auto trades = engine.submit({2, Side::SELL, 100.5, 6});

    std::cout << "Executed " << trades.size() << " trade(s):\n";
    for (const auto& trade : trades) {
        std::cout << "  qty " << trade.quantity
                  << " @ " << trade.price
                  << " (buy " << trade.buy_order_id
                  << " vs sell " << trade.sell_order_id << ")\n";
    }

    std::cout << "Resting bids: " << engine.bids().order_count()
              << " | resting asks: " << engine.asks().order_count() << '\n';
    return 0;
}
