#include "csv_replay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class ReplayAction { NEW, CANCEL, REPLACE };

struct ParsedRow {
    std::uint64_t ts_ns = 0;
    std::uint64_t seq = 0;
    std::size_t row_index = 0;
    ReplayAction action = ReplayAction::NEW;

    int order_id = 0;
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    PriceTicks price_ticks = 0;
    int quantity = 0;
    TimeInForce tif = TimeInForce::GTC;

    PriceTicks new_price_ticks = 0;
    int new_quantity = 0;
};

constexpr std::size_t kColumnCount = 12;
constexpr std::array<const char*, kColumnCount> kExpectedHeader = {
    "ts_ns",
    "seq",
    "action",
    "order_id",
    "side",
    "type",
    "price",
    "qty",
    "tif",
    "new_price",
    "new_qty",
    "notes"
};

std::string trim_copy(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

bool split_csv_line(const std::string& line,
                    std::vector<std::string>& out_fields,
                    std::string& out_error) {
    out_fields.clear();
    std::string field;
    bool in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
            continue;
        }

        if (ch == ',' && !in_quotes) {
            out_fields.push_back(trim_copy(field));
            field.clear();
            continue;
        }

        field.push_back(ch);
    }

    if (in_quotes) {
        out_error = "unterminated quoted field";
        return false;
    }

    out_fields.push_back(trim_copy(field));
    return true;
}

template <typename NumberType>
bool parse_number(const std::string& value, NumberType& out_number) {
    std::istringstream iss(value);
    iss >> out_number;
    if (iss.fail()) {
        return false;
    }

    char trailing = '\0';
    if (iss >> trailing) {
        return false;
    }
    return true;
}

bool parse_u64(const std::string& value, std::uint64_t& out_value) {
    if (value.empty() || value.front() == '-') {
        return false;
    }
    return parse_number(value, out_value);
}

bool parse_int(const std::string& value, int& out_value) {
    if (value.empty()) {
        return false;
    }
    return parse_number(value, out_value);
}

bool parse_price_ticks(const std::string& value, PriceTicks& out_ticks) {
    if (value.empty()) {
        return false;
    }

    double price = 0.0;
    if (!parse_number(value, price)) {
        return false;
    }
    out_ticks = price_to_ticks(price);
    return true;
}

bool parse_action(const std::string& value, ReplayAction& out_action) {
    if (value == "NEW") {
        out_action = ReplayAction::NEW;
        return true;
    }
    if (value == "CANCEL") {
        out_action = ReplayAction::CANCEL;
        return true;
    }
    if (value == "REPLACE") {
        out_action = ReplayAction::REPLACE;
        return true;
    }
    return false;
}

bool parse_side(const std::string& value, Side& out_side) {
    if (value == "BUY") {
        out_side = Side::BUY;
        return true;
    }
    if (value == "SELL") {
        out_side = Side::SELL;
        return true;
    }
    return false;
}

bool parse_order_type(const std::string& value, OrderType& out_type) {
    if (value == "LIMIT") {
        out_type = OrderType::LIMIT;
        return true;
    }
    if (value == "MARKET") {
        out_type = OrderType::MARKET;
        return true;
    }
    return false;
}

bool parse_tif(const std::string& value, TimeInForce& out_tif) {
    if (value.empty() || value == "GTC") {
        out_tif = TimeInForce::GTC;
        return true;
    }
    if (value == "IOC") {
        out_tif = TimeInForce::IOC;
        return true;
    }
    return false;
}

std::string line_error(std::size_t line_no, const std::string& message) {
    std::ostringstream oss;
    oss << "line " << line_no << ": " << message;
    return oss.str();
}

bool check_header(const std::vector<std::string>& fields, std::string& out_error) {
    if (fields.size() != kColumnCount) {
        std::ostringstream oss;
        oss << "invalid header: expected " << kColumnCount << " columns";
        out_error = oss.str();
        return false;
    }

    for (std::size_t i = 0; i < kColumnCount; ++i) {
        if (fields[i] != kExpectedHeader[i]) {
            std::ostringstream oss;
            oss << "invalid header column " << (i + 1) << ": expected '" << kExpectedHeader[i]
                << "' but found '" << fields[i] << "'";
            out_error = oss.str();
            return false;
        }
    }
    return true;
}

bool parse_row(const std::vector<std::string>& fields,
               std::size_t line_no,
               std::size_t row_index,
               ParsedRow& out_row,
               std::string& out_error) {
    if (fields.size() != kColumnCount) {
        std::ostringstream oss;
        oss << "expected " << kColumnCount << " columns, found " << fields.size();
        out_error = line_error(line_no, oss.str());
        return false;
    }

    if (!parse_u64(fields[0], out_row.ts_ns)) {
        out_error = line_error(line_no, "invalid ts_ns");
        return false;
    }
    if (!parse_u64(fields[1], out_row.seq)) {
        out_error = line_error(line_no, "invalid seq");
        return false;
    }
    if (!parse_action(fields[2], out_row.action)) {
        out_error = line_error(line_no, "invalid action (expected NEW/CANCEL/REPLACE)");
        return false;
    }

    out_row.row_index = row_index;
    if (!parse_int(fields[3], out_row.order_id) || out_row.order_id <= 0) {
        out_error = line_error(line_no, "invalid order_id (expected positive integer)");
        return false;
    }

    if (out_row.action == ReplayAction::NEW) {
        if (!parse_side(fields[4], out_row.side)) {
            out_error = line_error(line_no, "invalid side (expected BUY/SELL)");
            return false;
        }
        if (!parse_order_type(fields[5], out_row.type)) {
            out_error = line_error(line_no, "invalid type (expected LIMIT/MARKET)");
            return false;
        }

        if (out_row.type == OrderType::LIMIT) {
            if (!parse_price_ticks(fields[6], out_row.price_ticks) || out_row.price_ticks <= 0) {
                out_error = line_error(line_no, "invalid price for LIMIT order");
                return false;
            }
        } else {
            out_row.price_ticks = 0;
        }

        if (!parse_int(fields[7], out_row.quantity) || out_row.quantity <= 0) {
            out_error = line_error(line_no, "invalid qty (expected positive integer)");
            return false;
        }

        if (!parse_tif(fields[8], out_row.tif)) {
            out_error = line_error(line_no, "invalid tif (expected GTC/IOC)");
            return false;
        }
        return true;
    }

    if (out_row.action == ReplayAction::CANCEL) {
        return true;
    }

    if (!parse_price_ticks(fields[9], out_row.new_price_ticks) || out_row.new_price_ticks <= 0) {
        out_error = line_error(line_no, "invalid new_price for REPLACE");
        return false;
    }
    if (!parse_int(fields[10], out_row.new_quantity) || out_row.new_quantity <= 0) {
        out_error = line_error(line_no, "invalid new_qty for REPLACE");
        return false;
    }
    return true;
}

bool parse_csv_rows(const std::string& csv_path,
                    std::vector<ParsedRow>& out_rows,
                    std::string& out_error) {
    out_rows.clear();

    std::ifstream input(csv_path);
    if (!input.is_open()) {
        out_error = "failed to open CSV file: " + csv_path;
        return false;
    }

    std::string line;
    std::vector<std::string> fields;

    if (!std::getline(input, line)) {
        out_error = "CSV file is empty";
        return false;
    }
    std::string split_error;
    if (!split_csv_line(line, fields, split_error)) {
        out_error = line_error(1, split_error);
        return false;
    }
    if (!check_header(fields, out_error)) {
        out_error = line_error(1, out_error);
        return false;
    }

    std::size_t row_index = 0;
    std::size_t line_no = 1;
    while (std::getline(input, line)) {
        ++line_no;
        if (trim_copy(line).empty()) {
            continue;
        }

        if (!split_csv_line(line, fields, split_error)) {
            out_error = line_error(line_no, split_error);
            return false;
        }

        ParsedRow row;
        if (!parse_row(fields, line_no, row_index, row, out_error)) {
            return false;
        }

        out_rows.push_back(row);
        ++row_index;
    }

    return true;
}

std::string format_price(PriceTicks ticks) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << ticks_to_price(ticks);
    return oss.str();
}

}  // namespace

bool replay_csv_file(const std::string& csv_path,
                     MatchingEngine& engine,
                     ReplayResult& out_result,
                     std::string& out_error) {
    out_result = ReplayResult{};

    std::vector<ParsedRow> rows;
    if (!parse_csv_rows(csv_path, rows, out_error)) {
        return false;
    }

    std::sort(rows.begin(), rows.end(), [](const ParsedRow& lhs, const ParsedRow& rhs) {
        if (lhs.ts_ns != rhs.ts_ns) {
            return lhs.ts_ns < rhs.ts_ns;
        }
        if (lhs.seq != rhs.seq) {
            return lhs.seq < rhs.seq;
        }
        return lhs.row_index < rhs.row_index;
    });

    for (const auto& row : rows) {
        ++out_result.stats.rows_processed;

        if (row.action == ReplayAction::NEW) {
            SubmitResult result = engine.submit(
                {row.order_id, row.side, row.price_ticks, row.quantity, row.tif, row.type});
            if (result.accepted) {
                ++out_result.stats.accepted_actions;
            } else {
                ++out_result.stats.rejected_actions;
            }

            out_result.stats.trades_generated += result.trades.size();
            for (const auto& trade : result.trades) {
                out_result.trades.push_back(
                    {row.ts_ns,
                     row.seq,
                     trade.buy_order_id,
                     trade.sell_order_id,
                     trade.price_ticks,
                     trade.quantity});
            }
            continue;
        }

        if (row.action == ReplayAction::CANCEL) {
            if (engine.cancel(row.order_id)) {
                ++out_result.stats.accepted_actions;
                ++out_result.stats.cancel_success;
            } else {
                ++out_result.stats.rejected_actions;
                ++out_result.stats.cancel_not_found;
            }
            continue;
        }

        SubmitResult result = engine.replace(row.order_id, row.new_price_ticks, row.new_quantity);
        if (result.accepted) {
            ++out_result.stats.accepted_actions;
        } else {
            ++out_result.stats.rejected_actions;
        }

        out_result.stats.trades_generated += result.trades.size();
        for (const auto& trade : result.trades) {
            out_result.trades.push_back(
                {row.ts_ns,
                 row.seq,
                 trade.buy_order_id,
                 trade.sell_order_id,
                 trade.price_ticks,
                 trade.quantity});
        }
    }

    return true;
}

bool write_replay_trades_csv(const std::string& csv_path,
                             const std::vector<ReplayTradeRecord>& trades,
                             std::string& out_error) {
    std::ofstream output(csv_path);
    if (!output.is_open()) {
        out_error = "failed to open output CSV for writing: " + csv_path;
        return false;
    }

    output << "ts_ns,seq,buy_order_id,sell_order_id,price_ticks,price,quantity\n";
    for (const auto& trade : trades) {
        output << trade.ts_ns << ','
               << trade.seq << ','
               << trade.buy_order_id << ','
               << trade.sell_order_id << ','
               << trade.price_ticks << ','
               << format_price(trade.price_ticks) << ','
               << trade.quantity << '\n';
    }

    if (!output.good()) {
        out_error = "failed while writing output CSV: " + csv_path;
        return false;
    }

    return true;
}
