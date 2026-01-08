# Matching Engine (Mini Exchange)

Simple, single-threaded limit order book in C++ with price-time priority. It keeps bids/asks in memory, supports limit orders, partial fills, and emits trades when orders match.

## Layout
- `src/` core matching engine and book code.
- `tests/` minimal assertions to verify matching and partial fills.

## Build and run
```bash
mkdir -p build
cd build
cmake ..
cmake --build .
./matching_engine_app
./test_matching
```

## Next steps
- Add market orders, cancel by id, and a simple benchmarking harness.
