#pragma once

enum class Side { BUY, SELL };
enum class TimeInForce { GTC, IOC };

struct Order {
    int id;
    Side side;
    double price;
    int quantity;
    TimeInForce tif = TimeInForce::GTC;
};

struct Trade {
    int buy_order_id;
    int sell_order_id;
    double price;
    int quantity;
};
