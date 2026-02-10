#pragma once

#include <vector>

#include "order_book.h"

class MatchingEngine {
public:
    std::vector<Trade> submit(Order order);
    bool cancel(int order_id);

    const OrderBook& bids() const { return bids_; }
    const OrderBook& asks() const { return asks_; }

private:
    OrderBook bids_{Side::BUY};
    OrderBook asks_{Side::SELL};
};
