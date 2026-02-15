#pragma once

#include <cmath>
#include <cstdint>

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
