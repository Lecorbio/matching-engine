#include "csv_replay.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "replay_rows.h"

namespace {

std::string format_price(PriceTicks ticks) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << ticks_to_price(ticks);
    return oss.str();
}

void append_replay_trades(std::uint64_t ts_ns,
                          std::uint64_t seq,
                          const std::vector<Trade>& trades,
                          ReplayResult& replay) {
    replay.stats.trades_generated += trades.size();
    for (const auto& trade : trades) {
        replay.trades.push_back(
            {ts_ns, seq, trade.buy_order_id, trade.sell_order_id, trade.price_ticks, trade.quantity});
    }
}

}  // namespace

bool replay_csv_file(const std::string& csv_path,
                     MatchingEngine& engine,
                     ReplayResult& out_result,
                     std::string& out_error) {
    out_result = ReplayResult{};

    std::vector<ReplayRow> rows;
    if (!parse_replay_csv_rows(csv_path, rows, out_error)) {
        return false;
    }
    sort_replay_rows(rows);

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
            append_replay_trades(row.ts_ns, row.seq, result.trades, out_result);
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
        append_replay_trades(row.ts_ns, row.seq, result.trades, out_result);
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
