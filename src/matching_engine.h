#pragma once

#include <cstddef>
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

    bool has_order(int order_id) const;
    const OrderBook& bids() const { return bids_; }
    const OrderBook& asks() const { return asks_; }

private:
    OrderBook bids_{Side::BUY};
    OrderBook asks_{Side::SELL};
};
