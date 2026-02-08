#pragma once

enum class Side { BUY, SELL };

struct Order {
    int id;
    Side side;
    double price;
    int quantity;
};

struct Trade {
    int buy_order_id;
    int sell_order_id;
    double price;
    int quantity;
};
