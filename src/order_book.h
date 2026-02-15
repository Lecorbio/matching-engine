#pragma once

#include <cstddef>
#include <deque>
#include <map>

#include "types.h"

class OrderBook {
public:
    explicit OrderBook(Side side);

    void add(const Order& order);
    bool cancel(int order_id);
    void consume_best();

    bool contains(int order_id) const;
    bool empty() const;
    PriceTicks best_price_ticks() const;
    Order& best_order();
    const Order& best_order() const;
    std::size_t order_count() const;
    Side side() const;

private:
    struct PriceComparator {
        explicit PriceComparator(Side side) : side(side) {}
        bool operator()(PriceTicks lhs, PriceTicks rhs) const {
            return side == Side::BUY ? lhs > rhs : lhs < rhs;
        }
        Side side;
    };

    Side side_;
    PriceComparator comparator_;
    std::map<PriceTicks, std::deque<Order>, PriceComparator> levels_;
};
