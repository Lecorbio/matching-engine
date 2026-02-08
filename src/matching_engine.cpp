#include "matching_engine.h"

#include <algorithm>

std::vector<Trade> MatchingEngine::submit(Order order) {
    std::vector<Trade> trades;

    OrderBook& same_side = order.side == Side::BUY ? bids_ : asks_;
    OrderBook& opposite_side = order.side == Side::BUY ? asks_ : bids_;

    auto crosses = [&](double opposite_price) {
        if (order.side == Side::BUY) {
            return order.price >= opposite_price;
        }
        return order.price <= opposite_price;
    };

    while (order.quantity > 0 && !opposite_side.empty() && crosses(opposite_side.best_price())) {
        Order& resting = opposite_side.best_order();
        const int executed_qty = std::min(order.quantity, resting.quantity);

        trades.push_back({
            order.side == Side::BUY ? order.id : resting.id,
            order.side == Side::BUY ? resting.id : order.id,
            resting.price,
            executed_qty
        });

        order.quantity -= executed_qty;
        resting.quantity -= executed_qty;

        if (resting.quantity == 0) {
            opposite_side.consume_best();
        }
    }

    if (order.quantity > 0) {
        same_side.add(order);
    }

    return trades;
}
