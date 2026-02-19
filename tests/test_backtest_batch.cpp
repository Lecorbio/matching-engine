#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "backtest_batch.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
#endif

namespace {

std::string data_path(const std::string& filename) {
    return std::string(TEST_DATA_DIR) + "/" + filename;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    assert(input.is_open());

    std::ostringstream oss;
    oss << input.rdbuf();
    return oss.str();
}

std::size_t line_count(const std::string& text) {
    if (text.empty()) {
        return 0;
    }

    std::size_t lines = 0;
    for (const char ch : text) {
        if (ch == '\n') {
            ++lines;
        }
    }

    if (text.back() != '\n') {
        ++lines;
    }

    return lines;
}

}  // namespace

int main() {
    const std::filesystem::path tmp = std::filesystem::temp_directory_path();
    const std::filesystem::path requests_in = tmp / "matching_engine_batch_requests.csv";
    const std::filesystem::path runs_out = tmp / "matching_engine_batch_runs.csv";
    const std::filesystem::path summary_out = tmp / "matching_engine_batch_summary.csv";

    std::error_code ec;
    std::filesystem::remove(requests_in, ec);
    std::filesystem::remove(runs_out, ec);
    std::filesystem::remove(summary_out, ec);

    {
        std::ofstream request_file(requests_in);
        assert(request_file.is_open());
        request_file << "dataset,side,qty,slices,strategy\n";
        request_file << data_path("backtest_twap_basic.csv") << ",BUY,6,3,TWAP\n";
        request_file << data_path("backtest_twap_basic.csv") << ",BUY,6,3,VWAP\n";
        request_file << data_path("backtest_vwap_profile.csv") << ",BUY,7,3,TWAP\n";
        request_file << data_path("backtest_vwap_profile.csv") << ",BUY,7,3,VWAP\n";
        assert(request_file.good());
    }

    BatchRunStats stats;
    std::string error;
    const bool ok = run_backtest_batch_csv(
        requests_in.string(), runs_out.string(), summary_out.string(), stats, error);
    assert(ok);
    assert(stats.requests == 4);
    assert(stats.successful == 4);
    assert(stats.failed == 0);

    assert(std::filesystem::exists(runs_out));
    assert(std::filesystem::exists(summary_out));

    const std::string runs_text = read_text_file(runs_out);
    const std::string summary_text = read_text_file(summary_out);

    assert(line_count(runs_text) == 5);
    assert(runs_text.find("run_id,dataset,side,qty,slices,strategy,status") != std::string::npos);
    assert(runs_text.find(",TWAP,SUCCESS,") != std::string::npos);
    assert(runs_text.find(",VWAP,SUCCESS,") != std::string::npos);

    assert(summary_text.find("section,key,metric,count,mean,p50,p95") != std::string::npos);
    assert(summary_text.find("strategy,TWAP,shortfall_bps,2") != std::string::npos);
    assert(summary_text.find("strategy,VWAP,shortfall_bps,2") != std::string::npos);
    assert(summary_text.find("delta,TWAP_MINUS_VWAP,shortfall_bps_delta,2") != std::string::npos);

    return 0;
}
