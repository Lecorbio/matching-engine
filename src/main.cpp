#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "matching_engine.h"

namespace {

std::string fmt_price(PriceTicks price_ticks) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << ticks_to_price(price_ticks);
    return oss.str();
}

std::string fmt_level(const BookLevel& level) {
    std::ostringstream oss;
    oss << fmt_price(level.price_ticks) << " x " << level.quantity;
    return oss.str();
}

std::string fmt_optional_level(const std::optional<BookLevel>& level) {
    if (!level.has_value()) {
        return "--";
    }
    return fmt_level(level.value());
}

const char* reject_reason_to_cstr(RejectReason reason) {
    switch (reason) {
        case RejectReason::NONE:
            return "NONE";
        case RejectReason::INVALID_PRICE:
            return "INVALID_PRICE";
        case RejectReason::INVALID_QUANTITY:
            return "INVALID_QUANTITY";
        case RejectReason::DUPLICATE_ORDER_ID:
            return "DUPLICATE_ORDER_ID";
        case RejectReason::NO_LIQUIDITY:
            return "NO_LIQUIDITY";
        case RejectReason::ORDER_NOT_FOUND:
            return "ORDER_NOT_FOUND";
    }
    return "UNKNOWN";
}

void print_separator() {
    std::cout << "\n============================================================\n";
}

void print_trades(const std::vector<Trade>& trades) {
    if (trades.empty()) {
        std::cout << "Trades: none\n";
        return;
    }

    std::cout << "Trades (" << trades.size() << "):\n";
    std::cout << "  " << std::left
              << std::setw(8) << "BUY_ID"
              << std::setw(9) << "SELL_ID"
              << std::setw(11) << "PRICE"
              << "QTY\n";
    for (const auto& trade : trades) {
        std::cout << "  " << std::left
                  << std::setw(8) << trade.buy_order_id
                  << std::setw(9) << trade.sell_order_id
                  << std::setw(11) << fmt_price(trade.price_ticks)
                  << trade.quantity << '\n';
    }
}

void print_book(const MatchingEngine& engine, std::size_t n_levels) {
    const TopOfBook top = engine.top_of_book();
    const BookSnapshot snapshot = engine.depth(n_levels);

    std::cout << "Top of book: BID " << fmt_optional_level(top.best_bid)
              << " | ASK " << fmt_optional_level(top.best_ask) << '\n';
    if (top.best_bid.has_value() && top.best_ask.has_value()) {
        std::cout << "Spread: " << fmt_price(top.best_ask->price_ticks - top.best_bid->price_ticks)
                  << '\n';
    }

    std::cout << "Depth (top " << n_levels << " levels)\n";
    std::cout << std::left << std::setw(28) << "  BIDS" << "ASKS\n";
    const std::size_t rows = std::max(snapshot.bids.size(), snapshot.asks.size());
    for (std::size_t i = 0; i < rows; i++) {
        const std::string bid =
            i < snapshot.bids.size() ? fmt_level(snapshot.bids[i]) : std::string("--");
        const std::string ask =
            i < snapshot.asks.size() ? fmt_level(snapshot.asks[i]) : std::string("--");
        std::cout << "  " << std::left << std::setw(26) << bid << ask << '\n';
    }
    if (rows == 0) {
        std::cout << "  --                        --\n";
    }

    std::cout << "Resting orders: bids=" << engine.bids().order_count()
              << " asks=" << engine.asks().order_count() << '\n';
}

void print_submit_outcome(const SubmitResult& result) {
    std::cout << "Result: " << (result.accepted ? "ACCEPTED" : "REJECTED")
              << " (" << reject_reason_to_cstr(result.reject_reason) << ")\n";
    print_trades(result.trades);
}

}  // namespace

int main() {
    MatchingEngine engine;

    std::cout << "Matching Engine Demo\n";

    print_separator();
    std::cout << "Action: submit BUY #1001  qty=5  px=101.0000\n";
    print_submit_outcome(engine.submit({1001, Side::BUY, price_to_ticks(101.0), 5}));
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: submit BUY #1002  qty=4  px=100.5000\n";
    print_submit_outcome(engine.submit({1002, Side::BUY, price_to_ticks(100.5), 4}));
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: submit SELL #2001 qty=3  px=102.0000\n";
    print_submit_outcome(engine.submit({2001, Side::SELL, price_to_ticks(102.0), 3}));
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: submit SELL #2002 qty=6  px=103.0000\n";
    print_submit_outcome(engine.submit({2002, Side::SELL, price_to_ticks(103.0), 6}));
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: replace #1002 -> px=101.5000 qty=4\n";
    print_submit_outcome(engine.replace(1002, price_to_ticks(101.5), 4));
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: replace #1001 -> px=103.0000 qty=7 (crossing)\n";
    print_submit_outcome(engine.replace(1001, price_to_ticks(103.0), 7));
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: submit MARKET SELL #3001 qty=6\n";
    print_submit_outcome(
        engine.submit({3001, Side::SELL, 0, 6, TimeInForce::IOC, OrderType::MARKET}));
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: cancel #2002\n";
    std::cout << "Result: " << (engine.cancel(2002) ? "CANCELED" : "NOT_FOUND") << '\n';
    print_book(engine, 5);

    print_separator();
    std::cout << "Action: submit MARKET BUY #3002 qty=1 (no liquidity)\n";
    print_submit_outcome(
        engine.submit({3002, Side::BUY, 0, 1, TimeInForce::IOC, OrderType::MARKET}));
    print_book(engine, 5);

    print_separator();
    std::cout << "Demo complete.\n";
    return 0;
}
