#include "matching_engine.h"

#include <algorithm>

void MatchingEngine::push_event(BookEvent event) {
    event.seq_num = next_seq_num_++;
    events_.push_back(event);
}

void MatchingEngine::push_trade_event(const Trade& trade) {
    BookEvent event;
    event.type = BookEventType::TRADE;
    event.price_ticks = trade.price_ticks;
    event.quantity = trade.quantity;
    event.buy_order_id = trade.buy_order_id;
    event.sell_order_id = trade.sell_order_id;
    push_event(event);
}

void MatchingEngine::push_add_event(const Order& order) {
    BookEvent event;
    event.type = BookEventType::ADD;
    event.order_id = order.id;
    event.side = order.side;
    event.price_ticks = order.price_ticks;
    event.quantity = order.quantity;
    push_event(event);
}

void MatchingEngine::push_cancel_event(const Order& order) {
    BookEvent event;
    event.type = BookEventType::CANCEL;
    event.order_id = order.id;
    event.side = order.side;
    event.price_ticks = order.price_ticks;
    event.quantity = order.quantity;
    push_event(event);
}

void MatchingEngine::push_replace_event(const Order& old_order, const Order& new_order) {
    BookEvent event;
    event.type = BookEventType::REPLACE;
    event.order_id = old_order.id;
    event.side = old_order.side;
    event.old_price_ticks = old_order.price_ticks;
    event.old_quantity = old_order.quantity;
    event.price_ticks = new_order.price_ticks;
    event.quantity = new_order.quantity;
    push_event(event);
}

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
        push_trade_event(result.trades.back());

        order.quantity -= executed_qty;
        resting.quantity -= executed_qty;

        if (resting.quantity == 0) {
            opposite_side.consume_best();
        }
    }

    if (order.quantity > 0 && order.type == OrderType::LIMIT && order.tif == TimeInForce::GTC) {
        same_side.add(order);
        push_add_event(order);
    }

    return result;
}

bool MatchingEngine::cancel(int order_id) {
    auto removed_bid = bids_.remove(order_id);
    if (removed_bid.has_value()) {
        push_cancel_event(removed_bid.value());
        return true;
    }

    auto removed_ask = asks_.remove(order_id);
    if (removed_ask.has_value()) {
        push_cancel_event(removed_ask.value());
        return true;
    }
    return false;
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
        const Order old_order = *existing;
        existing->quantity = new_quantity;
        push_replace_event(old_order, *existing);
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
    push_replace_event(removed.value(), replacement);

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

std::uint64_t MatchingEngine::last_seq_num() const {
    if (events_.empty()) {
        return 0;
    }
    return events_.back().seq_num;
}

std::vector<BookEvent> MatchingEngine::events_since(std::uint64_t seq_num) const {
    std::vector<BookEvent> events;
    for (const auto& event : events_) {
        if (event.seq_num > seq_num) {
            events.push_back(event);
        }
    }
    return events;
}

bool MatchingEngine::has_order(int order_id) const {
    return bids_.contains(order_id) || asks_.contains(order_id);
}
