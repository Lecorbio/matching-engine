#include "order_book.h"

#include <cassert>

OrderBook::OrderBook(Side side)
    : side_(side), comparator_(side), levels_(comparator_) {}

void OrderBook::add(const Order& order) {
    auto level_it = levels_.find(order.price_ticks);
    if (level_it == levels_.end()) {
        level_it = levels_.emplace(order.price_ticks, LevelQueue{}).first;
    }
    auto& queue = level_it->second;
    queue.push_back(order);

    auto order_it = queue.end();
    --order_it;
    [[maybe_unused]] const bool inserted =
        order_index_.emplace(order.id, Locator{level_it, order_it}).second;
    assert(inserted && "Duplicate order id added to OrderBook");
}

bool OrderBook::cancel(int order_id) {
    return remove(order_id).has_value();
}

Order* OrderBook::find_mutable(int order_id) {
    auto index_it = order_index_.find(order_id);
    if (index_it == order_index_.end()) {
        return nullptr;
    }

    return &(*index_it->second.order_it);
}

const Order* OrderBook::find(int order_id) const {
    auto index_it = order_index_.find(order_id);
    if (index_it == order_index_.end()) {
        return nullptr;
    }

    return &(*index_it->second.order_it);
}

std::optional<Order> OrderBook::remove(int order_id) {
    auto index_it = order_index_.find(order_id);
    if (index_it == order_index_.end()) {
        return std::nullopt;
    }

    auto level_it = index_it->second.level_it;
    auto order_it = index_it->second.order_it;
    Order removed = *order_it;
    level_it->second.erase(order_it);
    if (level_it->second.empty()) {
        levels_.erase(level_it);
    }

    order_index_.erase(index_it);
    return removed;
}

void OrderBook::consume_best() {
    if (levels_.empty()) {
        return;
    }

    auto level_it = levels_.begin();
    auto& queue = level_it->second;
    if (!queue.empty()) {
        order_index_.erase(queue.front().id);
        queue.pop_front();
    }

    if (queue.empty()) {
        levels_.erase(level_it);
    }
}

bool OrderBook::contains(int order_id) const {
    return order_index_.find(order_id) != order_index_.end();
}

bool OrderBook::empty() const {
    return levels_.empty();
}

PriceTicks OrderBook::best_price_ticks() const {
    return levels_.begin()->first;
}

Order& OrderBook::best_order() {
    return levels_.begin()->second.front();
}

const Order& OrderBook::best_order() const {
    return levels_.begin()->second.front();
}

std::vector<BookLevel> OrderBook::depth(std::size_t n_levels) const {
    std::vector<BookLevel> levels;
    if (n_levels == 0) {
        return levels;
    }

    const std::size_t max_levels = n_levels < levels_.size() ? n_levels : levels_.size();
    levels.reserve(max_levels);

    for (const auto& [price_ticks, queue] : levels_) {
        int level_quantity = 0;
        for (const auto& order : queue) {
            level_quantity += order.quantity;
        }

        levels.push_back(BookLevel{price_ticks, level_quantity});
        if (levels.size() == n_levels) {
            break;
        }
    }

    return levels;
}

std::size_t OrderBook::order_count() const {
    return order_index_.size();
}

Side OrderBook::side() const {
    return side_;
}
