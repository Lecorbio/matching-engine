#include "order_book.h"

#include <numeric>

OrderBook::OrderBook(Side side)
    : side_(side), comparator_(side), levels_(comparator_) {}

void OrderBook::add(const Order& order) {
    levels_[order.price].push_back(order);
}

bool OrderBook::empty() const {
    return levels_.empty();
}

double OrderBook::best_price() const {
    return levels_.begin()->first;
}

Order& OrderBook::best_order() {
    return levels_.begin()->second.front();
}

const Order& OrderBook::best_order() const {
    return levels_.begin()->second.front();
}

void OrderBook::consume_best() {
    if (levels_.empty()) {
        return;
    }

    auto it = levels_.begin();
    if (!it->second.empty()) {
        it->second.pop_front();
    }

    if (it->second.empty()) {
        levels_.erase(it);
    }
}

std::size_t OrderBook::order_count() const {
    std::size_t total = 0;
    for (const auto& [_, queue] : levels_) {
        total += queue.size();
    }
    return total;
}

Side OrderBook::side() const {
    return side_;
}
