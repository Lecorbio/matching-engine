#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

enum class ReplayAction { NEW, CANCEL, REPLACE };

struct ReplayRow {
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

bool parse_replay_csv_rows(const std::string& csv_path,
                           std::vector<ReplayRow>& out_rows,
                           std::string& out_error);

void sort_replay_rows(std::vector<ReplayRow>& rows);
