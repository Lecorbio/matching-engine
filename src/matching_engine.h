#pragma once

#include <vector>

#include "order_book.h"

enum class RejectReason {
    NONE,
    INVALID_PRICE,
    INVALID_QUANTITY,
    DUPLICATE_ORDER_ID
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

    bool has_order(int order_id) const;
    const OrderBook& bids() const { return bids_; }
    const OrderBook& asks() const { return asks_; }

private:
    OrderBook bids_{Side::BUY};
    OrderBook asks_{Side::SELL};
};
