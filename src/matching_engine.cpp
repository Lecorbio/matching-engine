#include "matching_engine.h"

#include <algorithm>

SubmitResult MatchingEngine::submit(Order order) {
    SubmitResult result;

    if (order.quantity <= 0) {
        result.reject_reason = RejectReason::INVALID_QUANTITY;
        return result;
    }
    if (order.type == OrderType::LIMIT && order.price <= 0.0) {
        result.reject_reason = RejectReason::INVALID_PRICE;
        return result;
    }
    if (has_order(order.id)) {
        result.reject_reason = RejectReason::DUPLICATE_ORDER_ID;
        return result;
    }

    OrderBook& same_side = order.side == Side::BUY ? bids_ : asks_;
    OrderBook& opposite_side = order.side == Side::BUY ? asks_ : bids_;
    if (order.type == OrderType::MARKET && opposite_side.empty()) {
        result.reject_reason = RejectReason::NO_LIQUIDITY;
        return result;
    }

    result.accepted = true;
    result.reject_reason = RejectReason::NONE;

    auto crosses = [&](double opposite_price) {
        if (order.type == OrderType::MARKET) {
            return true;
        }
        if (order.side == Side::BUY) {
            return order.price >= opposite_price;
        }
        return order.price <= opposite_price;
    };

    while (order.quantity > 0 && !opposite_side.empty() && crosses(opposite_side.best_price())) {
        Order& resting = opposite_side.best_order();
        const int executed_qty = std::min(order.quantity, resting.quantity);

        result.trades.push_back({
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

    if (order.quantity > 0 && order.type == OrderType::LIMIT && order.tif == TimeInForce::GTC) {
        same_side.add(order);
    }

    return result;
}

bool MatchingEngine::cancel(int order_id) {
    if (bids_.cancel(order_id)) {
        return true;
    }
    return asks_.cancel(order_id);
}

bool MatchingEngine::has_order(int order_id) const {
    return bids_.contains(order_id) || asks_.contains(order_id);
}
