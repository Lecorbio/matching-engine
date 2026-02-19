# Mini Exchange Matching Engine

This project explores how a deterministic C++ limit order book behaves under replayed market flow,
and how basic execution schedules (TWAP/VWAP) perform in offline backtests.

Implemented scope:
- Single-threaded in-memory limit order book with price-time priority.
- Order handling: `LIMIT`/`MARKET`, `GTC`/`IOC`, partial fills, cancel, replace, and explicit reject reasons.
- Queue-priority replace rules:
  - same price + quantity decrease keeps priority,
  - price change or quantity increase loses priority.
- Market data APIs: top-of-book, depth snapshots, and incremental sequenced events (`ADD`, `TRADE`, `CANCEL`, `REPLACE`).
- Deterministic CSV replay (sorted by `ts_ns`, `seq`, and original row order).
- Execution backtests:
  - `backtest_twap`
  - `backtest_vwap`
  - `backtest_compare` (TWAP vs VWAP)
- Batch experiment runner (`backtest_batch`) that reads request CSVs and writes:
  - per-run outputs (`results/backtest_runs.csv`)
  - aggregated strategy summaries (`results/backtest_summary.csv`)
- TCA metrics include fill rate, average fill price, arrival benchmark, implementation shortfall (bps), and participation rate.
- GitHub Actions CI runs configure/build/test on push and pull request.

Design goals:
- Keep logic readable and beginner-friendly.
- Keep outputs deterministic and reproducible.
- Make strategy comparisons easy to run from CLI.

Current limitations:
- No networking, persistence layer, or concurrency model.
- No production/live execution adapter.
- VWAP sizing uses realized replay volume (look-ahead), so it is intended for offline benchmarking only.
- Performance benchmarking is not yet a primary module.
