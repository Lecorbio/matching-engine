#pragma once

#include <cstddef>
#include <string>

struct BatchRunStats {
    std::size_t requests = 0;
    std::size_t successful = 0;
    std::size_t failed = 0;
};

bool run_backtest_batch_csv(const std::string& requests_csv_path,
                            const std::string& runs_output_csv_path,
                            const std::string& summary_output_csv_path,
                            BatchRunStats& out_stats,
                            std::string& out_error);
