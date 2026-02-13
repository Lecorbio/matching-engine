#pragma once

#include <vector>

#include "order_book.h"

struct SubmitResult {
    bool accepted = false;
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
