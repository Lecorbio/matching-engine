#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "types.h"

class OrderBook {
public:
    explicit OrderBook(Side side);

    void add(const Order& order);
    bool cancel(int order_id);
    void consume_best();

    bool contains(int order_id) const;
    Order* find_mutable(int order_id);
    const Order* find(int order_id) const;
    std::optional<Order> remove(int order_id);
    bool empty() const;
    PriceTicks best_price_ticks() const;
    Order& best_order();
    const Order& best_order() const;
    std::vector<BookLevel> depth(std::size_t n_levels) const;
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

    using LevelQueue = std::list<Order>;
    using Levels = std::map<PriceTicks, LevelQueue, PriceComparator>;

    struct Locator {
        Levels::iterator level_it;
        LevelQueue::iterator order_it;
    };

    Side side_;
    PriceComparator comparator_;
    Levels levels_;
    std::unordered_map<int, Locator> order_index_;
};
