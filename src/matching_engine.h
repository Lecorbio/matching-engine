#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "order_book.h"

enum class RejectReason {
    NONE,
    INVALID_PRICE,
    INVALID_QUANTITY,
    DUPLICATE_ORDER_ID,
    NO_LIQUIDITY,
    ORDER_NOT_FOUND
};

struct SubmitResult {
    bool accepted = false;
    RejectReason reject_reason = RejectReason::NONE;
    std::vector<Trade> trades;
};

class MatchingEngine {
public:
    SubmitResult submit(Order order);
    bool cancel(int order_id);
    SubmitResult replace(int order_id, PriceTicks new_price_ticks, int new_quantity);
    TopOfBook top_of_book() const;
    BookSnapshot depth(std::size_t n_levels) const;
    std::uint64_t last_seq_num() const;
    std::vector<BookEvent> events_since(std::uint64_t seq_num) const;
    const std::vector<BookEvent>& event_log() const { return events_; }

    bool has_order(int order_id) const;
    const OrderBook& bids() const { return bids_; }
    const OrderBook& asks() const { return asks_; }

private:
    void push_event(BookEvent event);
    void push_trade_event(const Trade& trade);
    void push_add_event(const Order& order);
    void push_cancel_event(const Order& order);
    void push_replace_event(const Order& old_order, const Order& new_order);

    OrderBook bids_{Side::BUY};
    OrderBook asks_{Side::SELL};
    std::vector<BookEvent> events_;
    std::uint64_t next_seq_num_ = 1;
};
