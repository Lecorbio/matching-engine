#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

enum class Side { BUY, SELL };
enum class TimeInForce { GTC, IOC };
enum class OrderType { LIMIT, MARKET };

using PriceTicks = std::int64_t;
constexpr PriceTicks kTicksPerUnit = 10000;

inline PriceTicks price_to_ticks(double price) {
    return static_cast<PriceTicks>(std::llround(price * static_cast<double>(kTicksPerUnit)));
}

inline double ticks_to_price(PriceTicks price_ticks) {
    return static_cast<double>(price_ticks) / static_cast<double>(kTicksPerUnit);
}

struct Order {
    int id;
    Side side;
    PriceTicks price_ticks;
    int quantity;
    TimeInForce tif = TimeInForce::GTC;
    OrderType type = OrderType::LIMIT;
};

struct Trade {
    int buy_order_id;
    int sell_order_id;
    PriceTicks price_ticks;
    int quantity;
};

struct BookLevel {
    PriceTicks price_ticks;
    int quantity;
};

struct TopOfBook {
    std::optional<BookLevel> best_bid;
    std::optional<BookLevel> best_ask;
};

struct BookSnapshot {
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
};

enum class BookEventType { ADD, TRADE, CANCEL, REPLACE };

struct BookEvent {
    std::uint64_t seq_num = 0;
    BookEventType type = BookEventType::ADD;

    std::optional<int> order_id;
    std::optional<Side> side;
    std::optional<PriceTicks> price_ticks;
    std::optional<int> quantity;

    std::optional<int> buy_order_id;
    std::optional<int> sell_order_id;

    std::optional<PriceTicks> old_price_ticks;
    std::optional<int> old_quantity;
};
