#include "matching_engine.h"

#include <algorithm>

SubmitResult MatchingEngine::submit(Order order) {
    SubmitResult result;

    if (order.quantity <= 0) {
        result.reject_reason = RejectReason::INVALID_QUANTITY;
        return result;
    }
    if (order.type == OrderType::LIMIT && order.price_ticks <= 0) {
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

    auto crosses = [&](PriceTicks opposite_price_ticks) {
        if (order.type == OrderType::MARKET) {
            return true;
        }
        if (order.side == Side::BUY) {
            return order.price_ticks >= opposite_price_ticks;
        }
        return order.price_ticks <= opposite_price_ticks;
    };

    while (order.quantity > 0 && !opposite_side.empty() &&
           crosses(opposite_side.best_price_ticks())) {
        Order& resting = opposite_side.best_order();
        const int executed_qty = std::min(order.quantity, resting.quantity);

        result.trades.push_back({
            order.side == Side::BUY ? order.id : resting.id,
            order.side == Side::BUY ? resting.id : order.id,
            resting.price_ticks,
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

SubmitResult MatchingEngine::replace(int order_id, PriceTicks new_price_ticks, int new_quantity) {
    SubmitResult result;

    if (new_quantity <= 0) {
        result.reject_reason = RejectReason::INVALID_QUANTITY;
        return result;
    }
    if (new_price_ticks <= 0) {
        result.reject_reason = RejectReason::INVALID_PRICE;
        return result;
    }

    OrderBook* same_side = nullptr;
    Order* existing = bids_.find_mutable(order_id);
    if (existing != nullptr) {
        same_side = &bids_;
    } else {
        existing = asks_.find_mutable(order_id);
        if (existing != nullptr) {
            same_side = &asks_;
        }
    }

    if (existing == nullptr || same_side == nullptr) {
        result.reject_reason = RejectReason::ORDER_NOT_FOUND;
        return result;
    }

    if (existing->price_ticks == new_price_ticks && new_quantity <= existing->quantity) {
        existing->quantity = new_quantity;
        result.accepted = true;
        result.reject_reason = RejectReason::NONE;
        return result;
    }

    auto removed = same_side->remove(order_id);
    if (!removed.has_value()) {
        result.reject_reason = RejectReason::ORDER_NOT_FOUND;
        return result;
    }

    Order replacement = removed.value();
    replacement.price_ticks = new_price_ticks;
    replacement.quantity = new_quantity;
    replacement.tif = TimeInForce::GTC;
    replacement.type = OrderType::LIMIT;

    return submit(replacement);
}

TopOfBook MatchingEngine::top_of_book() const {
    TopOfBook top;

    auto bid_depth = bids_.depth(1);
    if (!bid_depth.empty()) {
        top.best_bid = bid_depth.front();
    }

    auto ask_depth = asks_.depth(1);
    if (!ask_depth.empty()) {
        top.best_ask = ask_depth.front();
    }

    return top;
}

BookSnapshot MatchingEngine::depth(std::size_t n_levels) const {
    BookSnapshot snapshot;
    snapshot.bids = bids_.depth(n_levels);
    snapshot.asks = asks_.depth(n_levels);
    return snapshot;
}

bool MatchingEngine::has_order(int order_id) const {
    return bids_.contains(order_id) || asks_.contains(order_id);
}
