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

    bool empty() const;
    double best_price() const;
    Order& best_order();
    const Order& best_order() const;
    std::size_t order_count() const;
    Side side() const;

private:
    struct PriceComparator {
        explicit PriceComparator(Side side) : side(side) {}
        bool operator()(double lhs, double rhs) const {
            return side == Side::BUY ? lhs > rhs : lhs < rhs;
        }
        Side side;
    };

    Side side_;
    PriceComparator comparator_;
    std::map<double, std::deque<Order>, PriceComparator> levels_;
};
