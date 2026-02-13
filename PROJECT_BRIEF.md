# Mini Exchange Matching Engine

Goals:
- Implement a simple limit order book with price-time priority (best price first, FIFO at each price).
- Support limit orders, partial fills, cancel by order id, IOC/GTC time-in-force, and generate trade records on matches.
- Keep code minimal, readable, and beginner-friendly C++.

Core behavior:
- Separate bids/asks; maintain price levels with queues to preserve order arrival.
- Matching: an incoming order executes against the best opposite prices until filled or no match; leftover rests in book.
- Partial fills allowed; track remaining quantity per order.
- Cancel: remove a resting order by id and return whether cancel succeeded.
- Submit API returns `SubmitResult { accepted, reject_reason, trades }`.
- Safety checks reject duplicate ids and non-positive price/quantity with explicit reject reasons.
- Time-in-force supports:
  - `GTC` (rest leftover quantity),
  - `IOC` (cancel leftover quantity immediately).
- Emit trades with price, quantity, and the involved order ids.

Out of scope for MVP (future work):
- Market orders that cross the book fully.
- Benchmarking harness to measure throughput/latency.

Non-goals:
- Persistent storage, networking, or concurrency; keep it single-threaded and in-memory.
