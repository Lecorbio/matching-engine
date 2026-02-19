#include "backtest_batch.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "execution_backtest.h"

namespace {

struct BatchRequest {
    std::string dataset;
    Side side = Side::BUY;
    int quantity = 0;
    int slices = 0;
    ExecutionStrategy strategy = ExecutionStrategy::TWAP;
};

struct BatchRun {
    std::size_t run_id = 0;
    BatchRequest request;
    bool success = false;
    std::string error;
    BacktestResult result;
};

struct DistributionStats {
    std::size_t count = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
};

struct SummaryRow {
    std::string section;
    std::string key;
    std::string metric;
    DistributionStats stats;
};

constexpr std::size_t kBatchColumnCount = 5;
constexpr std::array<const char*, kBatchColumnCount> kBatchHeader = {
    "dataset", "side", "qty", "slices", "strategy"
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

std::string line_error(std::size_t line_no, const std::string& message) {
    std::ostringstream oss;
    oss << "line " << line_no << ": " << message;
    return oss.str();
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

bool parse_positive_int(const std::string& value, int& out_int) {
    if (!parse_number(value, out_int)) {
        return false;
    }
    return out_int > 0;
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

bool parse_strategy(const std::string& value, ExecutionStrategy& out_strategy) {
    if (value == "TWAP") {
        out_strategy = ExecutionStrategy::TWAP;
        return true;
    }
    if (value == "VWAP") {
        out_strategy = ExecutionStrategy::VWAP;
        return true;
    }
    return false;
}

bool check_header(const std::vector<std::string>& fields, std::string& out_error) {
    if (fields.size() != kBatchColumnCount) {
        std::ostringstream oss;
        oss << "invalid header: expected " << kBatchColumnCount << " columns";
        out_error = oss.str();
        return false;
    }

    for (std::size_t i = 0; i < kBatchColumnCount; ++i) {
        if (fields[i] != kBatchHeader[i]) {
            std::ostringstream oss;
            oss << "invalid header column " << (i + 1) << ": expected '" << kBatchHeader[i]
                << "' but found '" << fields[i] << "'";
            out_error = oss.str();
            return false;
        }
    }

    return true;
}

bool parse_request_row(const std::vector<std::string>& fields,
                       std::size_t line_no,
                       BatchRequest& out_request,
                       std::string& out_error) {
    if (fields.size() != kBatchColumnCount) {
        std::ostringstream oss;
        oss << "expected " << kBatchColumnCount << " columns, found " << fields.size();
        out_error = line_error(line_no, oss.str());
        return false;
    }

    out_request.dataset = fields[0];
    if (out_request.dataset.empty()) {
        out_error = line_error(line_no, "dataset cannot be empty");
        return false;
    }

    if (!parse_side(fields[1], out_request.side)) {
        out_error = line_error(line_no, "invalid side (expected BUY/SELL)");
        return false;
    }

    if (!parse_positive_int(fields[2], out_request.quantity)) {
        out_error = line_error(line_no, "invalid qty (expected positive integer)");
        return false;
    }

    if (!parse_positive_int(fields[3], out_request.slices)) {
        out_error = line_error(line_no, "invalid slices (expected positive integer)");
        return false;
    }

    if (!parse_strategy(fields[4], out_request.strategy)) {
        out_error = line_error(line_no, "invalid strategy (expected TWAP/VWAP)");
        return false;
    }

    return true;
}

bool parse_requests_csv(const std::string& csv_path,
                        std::vector<BatchRequest>& out_requests,
                        std::string& out_error) {
    out_requests.clear();

    std::ifstream input(csv_path);
    if (!input.is_open()) {
        out_error = "failed to open batch CSV file: " + csv_path;
        return false;
    }

    std::string line;
    std::vector<std::string> fields;
    std::string split_error;

    if (!std::getline(input, line)) {
        out_error = "batch CSV file is empty";
        return false;
    }

    if (!split_csv_line(line, fields, split_error)) {
        out_error = line_error(1, split_error);
        return false;
    }

    if (!check_header(fields, out_error)) {
        out_error = line_error(1, out_error);
        return false;
    }

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

        BatchRequest request;
        if (!parse_request_row(fields, line_no, request, out_error)) {
            return false;
        }

        out_requests.push_back(request);
    }

    if (out_requests.empty()) {
        out_error = "batch CSV has no request rows";
        return false;
    }

    return true;
}

const char* side_to_cstr(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

const char* strategy_to_cstr(ExecutionStrategy strategy) {
    switch (strategy) {
        case ExecutionStrategy::TWAP:
            return "TWAP";
        case ExecutionStrategy::VWAP:
            return "VWAP";
    }
    return "UNKNOWN";
}

std::string format_price(PriceTicks ticks) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << ticks_to_price(ticks);
    return oss.str();
}

std::string format_double(double value, int decimals = 6) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << value;
    return oss.str();
}

std::string csv_escape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }

    std::string escaped = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

bool ensure_parent_directory(const std::string& output_path, std::string& out_error) {
    const std::filesystem::path path(output_path);
    if (!path.has_parent_path()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        out_error = "failed to create output directory '" + path.parent_path().string() +
                    "': " + ec.message();
        return false;
    }

    return true;
}

bool write_runs_csv(const std::string& output_path,
                    const std::vector<BatchRun>& runs,
                    std::string& out_error) {
    if (!ensure_parent_directory(output_path, out_error)) {
        return false;
    }

    std::ofstream output(output_path);
    if (!output.is_open()) {
        out_error = "failed to open runs output CSV: " + output_path;
        return false;
    }

    output << "run_id,dataset,side,qty,slices,strategy,status,error,"
              "filled_qty,target_qty,fill_rate,avg_fill_price,"
              "arrival_benchmark_name,arrival_benchmark_price,shortfall_bps,participation_rate,"
              "replay_rows,replay_trades\n";

    for (const auto& run : runs) {
        output << run.run_id << ','
               << csv_escape(run.request.dataset) << ','
               << side_to_cstr(run.request.side) << ','
               << run.request.quantity << ','
               << run.request.slices << ','
               << strategy_to_cstr(run.request.strategy) << ','
               << (run.success ? "SUCCESS" : "FAILED") << ','
               << csv_escape(run.error) << ',';

        if (!run.success) {
            output << ",,,,,,,,\n";
            continue;
        }

        output << run.result.tca.filled_quantity << ','
               << run.result.tca.target_quantity << ','
               << format_double(run.result.tca.fill_rate) << ',';

        if (run.result.tca.average_fill_price_ticks.has_value()) {
            output << format_price(run.result.tca.average_fill_price_ticks.value());
        }
        output << ','
               << run.result.tca.arrival_benchmark_name << ',';

        if (run.result.tca.arrival_benchmark_price_ticks.has_value()) {
            output << format_price(run.result.tca.arrival_benchmark_price_ticks.value());
        }
        output << ',';

        if (run.result.tca.implementation_shortfall_bps.has_value()) {
            output << format_double(run.result.tca.implementation_shortfall_bps.value());
        }
        output << ','
               << format_double(run.result.tca.participation_rate) << ','
               << run.result.replay_stats.rows_processed << ','
               << run.result.replay_stats.trades_generated << '\n';
    }

    if (!output.good()) {
        out_error = "failed while writing runs output CSV: " + output_path;
        return false;
    }

    return true;
}

std::optional<double> percentile(const std::vector<double>& sorted_values, double p) {
    if (sorted_values.empty()) {
        return std::nullopt;
    }

    if (sorted_values.size() == 1) {
        return sorted_values.front();
    }

    const double index = p * static_cast<double>(sorted_values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(index));
    const auto upper = static_cast<std::size_t>(std::ceil(index));
    const double weight = index - static_cast<double>(lower);

    return sorted_values[lower] +
           (sorted_values[upper] - sorted_values[lower]) * weight;
}

std::optional<DistributionStats> compute_distribution_stats(std::vector<double> values) {
    if (values.empty()) {
        return std::nullopt;
    }

    std::sort(values.begin(), values.end());
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);

    const std::optional<double> p50 = percentile(values, 0.50);
    const std::optional<double> p95 = percentile(values, 0.95);
    if (!p50.has_value() || !p95.has_value()) {
        return std::nullopt;
    }

    DistributionStats stats;
    stats.count = values.size();
    stats.mean = sum / static_cast<double>(values.size());
    stats.p50 = p50.value();
    stats.p95 = p95.value();
    return stats;
}

std::string scenario_key(const BatchRequest& request) {
    std::ostringstream oss;
    oss << request.dataset << '|'
        << side_to_cstr(request.side) << '|'
        << request.quantity << '|'
        << request.slices;
    return oss.str();
}

std::vector<double> paired_delta(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    const std::size_t count = std::min(lhs.size(), rhs.size());
    std::vector<double> delta;
    delta.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        delta.push_back(lhs[i] - rhs[i]);
    }
    return delta;
}

void push_strategy_summary_rows(const char* strategy_name,
                                const std::vector<double>& fill_rate,
                                const std::vector<double>& shortfall,
                                const std::vector<double>& participation,
                                std::vector<SummaryRow>& out_rows) {
    const std::optional<DistributionStats> fill_stats = compute_distribution_stats(fill_rate);
    if (fill_stats.has_value()) {
        out_rows.push_back({"strategy", strategy_name, "fill_rate", fill_stats.value()});
    }

    const std::optional<DistributionStats> shortfall_stats = compute_distribution_stats(shortfall);
    if (shortfall_stats.has_value()) {
        out_rows.push_back({"strategy", strategy_name, "shortfall_bps", shortfall_stats.value()});
    }

    const std::optional<DistributionStats> participation_stats =
        compute_distribution_stats(participation);
    if (participation_stats.has_value()) {
        out_rows.push_back(
            {"strategy", strategy_name, "participation_rate", participation_stats.value()});
    }
}

void build_summary_rows(const std::vector<BatchRun>& runs, std::vector<SummaryRow>& out_rows) {
    std::vector<double> twap_fill_rate;
    std::vector<double> twap_shortfall;
    std::vector<double> twap_participation;

    std::vector<double> vwap_fill_rate;
    std::vector<double> vwap_shortfall;
    std::vector<double> vwap_participation;

    struct StrategyValuesByScenario {
        std::vector<double> fill_rate;
        std::vector<double> shortfall;
        std::vector<double> participation;
    };

    struct ScenarioValues {
        StrategyValuesByScenario twap;
        StrategyValuesByScenario vwap;
    };

    std::map<std::string, ScenarioValues> by_scenario;

    for (const auto& run : runs) {
        if (!run.success) {
            continue;
        }

        const double fill_rate = run.result.tca.fill_rate;
        const double participation = run.result.tca.participation_rate;

        StrategyValuesByScenario* scenario_values = nullptr;
        if (run.request.strategy == ExecutionStrategy::TWAP) {
            twap_fill_rate.push_back(fill_rate);
            twap_participation.push_back(participation);
            scenario_values = &by_scenario[scenario_key(run.request)].twap;
        } else {
            vwap_fill_rate.push_back(fill_rate);
            vwap_participation.push_back(participation);
            scenario_values = &by_scenario[scenario_key(run.request)].vwap;
        }

        scenario_values->fill_rate.push_back(fill_rate);
        scenario_values->participation.push_back(participation);

        if (run.result.tca.implementation_shortfall_bps.has_value()) {
            const double shortfall = run.result.tca.implementation_shortfall_bps.value();
            if (run.request.strategy == ExecutionStrategy::TWAP) {
                twap_shortfall.push_back(shortfall);
            } else {
                vwap_shortfall.push_back(shortfall);
            }
            scenario_values->shortfall.push_back(shortfall);
        }
    }

    push_strategy_summary_rows(
        "TWAP", twap_fill_rate, twap_shortfall, twap_participation, out_rows);
    push_strategy_summary_rows(
        "VWAP", vwap_fill_rate, vwap_shortfall, vwap_participation, out_rows);

    std::vector<double> delta_fill_rate;
    std::vector<double> delta_shortfall;
    std::vector<double> delta_participation;

    for (const auto& [_, scenario] : by_scenario) {
        const std::vector<double> fill_delta =
            paired_delta(scenario.twap.fill_rate, scenario.vwap.fill_rate);
        delta_fill_rate.insert(delta_fill_rate.end(), fill_delta.begin(), fill_delta.end());

        const std::vector<double> shortfall_delta =
            paired_delta(scenario.twap.shortfall, scenario.vwap.shortfall);
        delta_shortfall.insert(delta_shortfall.end(), shortfall_delta.begin(), shortfall_delta.end());

        const std::vector<double> participation_delta =
            paired_delta(scenario.twap.participation, scenario.vwap.participation);
        delta_participation.insert(
            delta_participation.end(), participation_delta.begin(), participation_delta.end());
    }

    const std::optional<DistributionStats> delta_fill_stats =
        compute_distribution_stats(delta_fill_rate);
    if (delta_fill_stats.has_value()) {
        out_rows.push_back(
            {"delta", "TWAP_MINUS_VWAP", "fill_rate_delta", delta_fill_stats.value()});
    }

    const std::optional<DistributionStats> delta_shortfall_stats =
        compute_distribution_stats(delta_shortfall);
    if (delta_shortfall_stats.has_value()) {
        out_rows.push_back(
            {"delta", "TWAP_MINUS_VWAP", "shortfall_bps_delta", delta_shortfall_stats.value()});
    }

    const std::optional<DistributionStats> delta_participation_stats =
        compute_distribution_stats(delta_participation);
    if (delta_participation_stats.has_value()) {
        out_rows.push_back({"delta",
                            "TWAP_MINUS_VWAP",
                            "participation_rate_delta",
                            delta_participation_stats.value()});
    }
}

bool write_summary_csv(const std::string& output_path,
                       const std::vector<BatchRun>& runs,
                       std::string& out_error) {
    if (!ensure_parent_directory(output_path, out_error)) {
        return false;
    }

    std::ofstream output(output_path);
    if (!output.is_open()) {
        out_error = "failed to open summary output CSV: " + output_path;
        return false;
    }

    output << "section,key,metric,count,mean,p50,p95\n";

    std::vector<SummaryRow> rows;
    build_summary_rows(runs, rows);

    for (const auto& row : rows) {
        output << row.section << ','
               << row.key << ','
               << row.metric << ','
               << row.stats.count << ','
               << format_double(row.stats.mean) << ','
               << format_double(row.stats.p50) << ','
               << format_double(row.stats.p95) << '\n';
    }

    if (!output.good()) {
        out_error = "failed while writing summary output CSV: " + output_path;
        return false;
    }

    return true;
}

}  // namespace

bool run_backtest_batch_csv(const std::string& requests_csv_path,
                            const std::string& runs_output_csv_path,
                            const std::string& summary_output_csv_path,
                            BatchRunStats& out_stats,
                            std::string& out_error) {
    out_stats = BatchRunStats{};

    std::vector<BatchRequest> requests;
    if (!parse_requests_csv(requests_csv_path, requests, out_error)) {
        return false;
    }

    std::vector<BatchRun> runs;
    runs.reserve(requests.size());

    for (std::size_t i = 0; i < requests.size(); ++i) {
        const BatchRequest& request = requests[i];

        BacktestConfig config;
        config.side = request.side;
        config.target_quantity = request.quantity;
        config.slices = static_cast<std::size_t>(request.slices);
        config.strategy = request.strategy;

        BatchRun run;
        run.run_id = i + 1;
        run.request = request;

        std::string run_error;
        run.success =
            run_execution_backtest_csv(request.dataset, config, run.result, run_error);
        run.error = run_error;

        if (run.success) {
            ++out_stats.successful;
        } else {
            ++out_stats.failed;
        }

        runs.push_back(std::move(run));
    }

    out_stats.requests = requests.size();

    if (!write_runs_csv(runs_output_csv_path, runs, out_error)) {
        return false;
    }

    if (!write_summary_csv(summary_output_csv_path, runs, out_error)) {
        return false;
    }

    return true;
}
