# Limit Order Book

A matching engine for a single instrument in C++, implemented twice with
different data structures, and benchmarked against each other on an identical
synthetic order stream.

The matching logic is the easy part and is well covered elsewhere. The point of
this project was the second half: measuring per-operation latency
*distributions* rather than throughput, finding the spikes, and explaining
where they came from.

## Build

```bash
g++ -std=c++20 -O2 -Wall -Wextra -o orderbook src/main.cpp
./orderbook
```

## What it does

Orders arrive with an id, side, price and quantity. An order that crosses the
spread executes immediately against resting orders; anything left over rests in
the book. Matching follows **price-time priority** — the best price wins, and at
equal price the order that arrived first is filled first. Trades execute at the
*resting* order's price, so an aggressive order can receive price improvement.

Three operations: add, cancel, and match (an add that crosses).

## Two implementations

**`OrderBook` — the textbook structure.** `std::map<int64_t, std::list<Order>>`
per side, with the bid side ordered by `std::greater<>` so that `begin()` is the
best price on both sides. Prices are integer ticks, not doubles, because `0.01`
has no exact binary representation and two orders at the "same" price must
compare equal.

**`FlatOrderBook` — a flat array.** Price levels live in a `std::vector` indexed
by offset from a fixed minimum tick, giving O(1) level lookup and contiguous
storage. The best bid and best ask indices are cached and advanced lazily as
levels empty — without that cache, finding the best price would mean scanning
the array, which is worse than the tree it replaced.

Both use an `unordered_map<OrderId, {side, price, list iterator}>` index so that
cancellation is O(1) rather than a search. This is the reason the levels are
`std::list` rather than `std::vector`: **list iterators remain valid when other
elements are erased**, so the stored iterators stay correct for the life of the
order. With a vector, erasing one order would invalidate every stored iterator
after it.

## Correctness

Nine self-checking tests covering exact-price matching, the one-tick-away
boundary, partial fills on both sides, time priority within a level, full sweeps
of one side, empty-book behaviour, and cancellation of already-filled orders
(which must be rejected, not crash).

A deliberately naive O(n) cancel is kept alongside the indexed one and used as a
correctness oracle: the same operation sequence is run through both and the
resulting books are asserted identical. Both implementations pass the same
suite.

## Benchmark method

A synthetic stream of 1,000,000 events — roughly 60% adds, 30% cancels, 10%
marketable orders — generated from a fixed seed so both implementations see a
byte-identical workload. Add prices are skewed to either side of the mid so that
adds rest and only match events cross; without that skew roughly half the "adds"
would be matches in disguise.

The stream is generated before timing begins, and the first 100,000 events run
untimed as a warm-up. Each remaining operation is timed individually with
`steady_clock` and every sample is kept, because the distribution is the point
and a running average would destroy it.

## Results

Latency in nanoseconds, same stream, same seed.

| Operation | Metric | `std::map` | flat array | change |
|---|---|---:|---:|---:|
| add | p99 | 125 | **84** | −33% |
| add | p99.9 | 1042 | **916** | −12% |
| add | max | 25,000 | **16,000** | −36% |
| cancel | p50 | 83 | **42** | −49% |
| cancel | p99 | 333 | **292** | −12% |
| cancel | p99.9 | 458 | **417** | −9% |
| match | p99 | 209 | **167** | −20% |
| match | p99.9 | 334 | **292** | −13% |

Batched timing (100 operations between clock reads, all operation types mixed):

| | `std::map` | flat array |
|---|---:|---:|
| median | 65.0 ns | **48.75 ns** |
| mean | 65.6 ns | **49.9 ns** |

**The flat array is about 25% faster on the typical operation and 9–36% better
through the tail.** The single exception is `match` max, where the flat version
was worse on this run; single-sample maxima are noisy and I would not read
anything into one figure.

## Two latency cliffs, found and fixed

Neither of these is visible in an average. Both showed up only as a maximum.

**Hash table rehashing — 190× on the worst case.** The first clean benchmark run
showed an `add` maximum of **3,652,167 ns** against a median of 83 ns: one
operation in a million was 44,000 times slower than typical. The cause was
`std::unordered_map` growing its bucket array and rehashing every entry, so a
single unlucky insert paid for all the others. Calling `reserve()` on the index
at construction dropped the maximum to **19,125 ns**.

**First-touch page cost — 4.5× on the worst case.** The flat book allocates 401
list headers per side up front, and its first benchmark showed an `add` maximum
of **103,792 ns** against the map's 19,041 — five times *worse*, contradicting
every other figure. Raising the warm-up from 10,000 to 100,000 events brought it
to **23,125 ns**, confirming the spike was the one-off cost of first touching
cold pages rather than anything structural.

Both are exactly why production trading systems pre-allocate everything and ban
allocation from the hot path.

## What I got wrong

- **Benchmarking `std::cout`.** The first benchmark run left trade logging
  enabled inside the matching loop. Console I/O is microseconds; the reported
  figures were three orders of magnitude too high and measured the terminal, not
  the book.
- **Four mirror-image bugs.** Buy/sell and asks/bids appear as near-identical
  blocks throughout, and four separate times one copy was updated and the other
  was not: a sell branch pushing into the bid book, a cancel erasing from the
  wrong side, a quantity guard applied to one branch only. Templating the two
  matchers into one function would have removed the entire class of error.
- **Reading the near-miss backwards.** I expected the flat array to win on the
  tail and be roughly equal on the median. It was the other way round — the
  median gap (25%) is larger than most of the tail gaps.
- **A segfault that the tests were written to catch.** Cancelling a filled order
  used a dangling list iterator, because the matchers removed orders from the
  book without removing them from the index.

## Limitations

- Single instrument, single threaded, no market orders or order modification
- Everything in one translation unit
- `steady_clock` resolution on this machine is ~42 ns, so single-operation
  medians sit at the measurement floor; the batched figures exist to get
  underneath it
- The flat array assumes a bounded price range known at compile time, which is
  reasonable for one instrument over one session and not in general
- Synthetic order flow, chosen deliberately: a controlled comparison needs the
  same stream through both implementations, which real captured data cannot
  provide
