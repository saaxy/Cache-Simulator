# CacheSim: interview preparation

Read the code and re-run the experiments before an interview; answer in your own words. Every number below comes from `results/sample_results.csv` or the test files, and you should be able to regenerate it (`cachesim_bench --synthetic all`). Do not claim anything the project does not do (see the honesty notes at the end).

## 60-second explanation

"I built a configurable CPU cache simulator in C++17. You feed it a trace of reads and writes with addresses, and it models a cache with a chosen size, block size and associativity, from direct-mapped to fully associative, with LRU or FIFO replacement and write-back or write-through with or without write-allocate. It reports hits, misses, evictions and memory traffic, and it can add an L2 and give a model-based AMAT estimate. The core idea is that an address splits into tag, set index and block offset; the set index picks a set, and the tag is compared with the lines in that set. I tested it with hand-calculated cases, a randomized comparison against an independent reference model, and end-to-end tests. Then I built a benchmark runner and plotting scripts to see how associativity, capacity and block size change the hit rate. For example, a strided pattern whose addresses all map to one set gets zero hits up to 4 ways and almost all hits at 8 ways. It is a functional model, so the results illustrate behavior and are not hardware measurements."

## 3-minute technical walkthrough

1. **Problem and scope (20 s).** Memory is slow, caches exploit locality. I wanted to understand the mechanics by building the model, so I made it a functional simulator: it tracks which blocks are present, not data or time.
2. **Configuration (20 s).** `CacheConfig` is immutable and validated in its constructor: sizes must be powers of two, block no larger than capacity, ways a power of two and no more than the number of lines. If an object exists, its derived values (sets, offset bits, index bits) are consistent. `split()` computes tag, index and offset with shifts and masks; fully associative is just one set, so the same code covers every organization.
3. **Cache core (45 s).** Lines are a flat `std::vector<CacheLine>`, with set `s` at `[s*ways, (s+1)*ways)`. An access scans the ways of one set. On a miss it picks an empty way or the way with the smallest stamp. One stamp field serves both policies: FIFO writes it only on fill, LRU also on every hit. Write policies are a small block of logic: write-back marks the line dirty and writes memory on dirty eviction; write-through forwards every store; no-write-allocate sends a write miss straight to the next level.
4. **Hierarchy (25 s).** `accessDetailed` reports what an access sent downstream: a fetch, a forwarded write, a dirty eviction with its address. `runSimulation` turns those into L2 accesses. AMAT uses the textbook formula with illustrative latencies and is labelled as an estimate.
5. **Tooling (20 s).** Trace parser with `file:line` errors, CSV output, a deterministic synthetic trace generator, a benchmark runner that loads a trace once and sweeps about 29 configurations, and a pandas/Matplotlib script.
6. **Testing (30 s).** 20 CTest tests: expected results derived by hand in comments; a randomized differential test against a naive reference model with a fixed seed; end-to-end tests of the executables. These show the code matches the documented semantics, not that it matches real hardware.
7. **Results and limits (20 s).** Strided trace: 0 hits up to 4-way, 0.9998 at 8-way. Block size: sequential improves from 0.75 to 0.98 as blocks grow, while the hot-set trace falls from 0.9994 to 0.57 at a fixed capacity. Limitations: no timing, no access sizes, no coherence, exact LRU only, AMAT is a model.

## Likely questions and answers

### Cache concepts

**1. How does an address map into the cache?**
Split it into block offset (low log2(block size) bits), set index (next log2(sets) bits) and tag (the rest). The index selects the set; the tag is compared against every valid line in that set. Example from my tests: 1 KiB direct-mapped, 16 B blocks gives offset 4 bits, index 6 bits, and `0x1234` is tag 4, index 35, offset 4.

**2. Direct-mapped vs set-associative vs fully associative?**
Direct-mapped has one candidate line, so it is fastest and cheapest but suffers conflict misses. N-way gives N candidates per set. Fully associative has no conflict misses but needs a search of every line (expensive in hardware). In my simulator they are the same code with a different number of ways. On the `hotset` trace the hit rate went from 0.738 (1-way) to 0.9994 (4-way).

**3. Why must sizes be powers of two?**
So the index and offset are plain bit fields that can be extracted with shifts and masks, as hardware does. With a non-power-of-two number of sets you would need a modulo.

**4. Compulsory, capacity and conflict misses; does your simulator classify them?**
Compulsory: first reference to a block. Capacity: the working set exceeds the cache. Conflict: evicted only because of the set mapping. My simulator does not classify them. To do it you track every block ever seen (compulsory) and run a parallel fully associative cache of the same size: a miss that also misses there is capacity, and one that hits there is conflict. The strided trace shows a conflict effect indirectly: 4 ways thrash, a fully associative cache of the same size does not.

**5. LRU vs FIFO?**
LRU evicts the least recently used line and refreshes recency on hits; FIFO evicts the oldest-inserted line and ignores hits. LRU exploits temporal locality, FIFO is simpler. Neither wins everywhere: in my tests `A B A C A` favors LRU, while `A B C B A C B` favors FIFO. FIFO is not a stack algorithm, so Belady's anomaly (more capacity, more misses) is possible in theory; my simulator requires power-of-two ways, so I cannot reproduce the classic 3-vs-4 example directly.

**6. Why does cycling through N+1 blocks in an N-way set defeat LRU?**
Each access evicts exactly the block that will be needed next, so every access misses. My `testCyclicThrashingMissesUnderBothPolicies` shows 9 misses out of 9 for both policies. It is also what the strided trace does for up to 4 ways.

**7. Write-through vs write-back?**
Write-through sends every store to the next level: simple and memory is always current, but heavy write traffic. Write-back marks the line dirty and writes it only on eviction: less traffic, but needs a dirty bit and dirty data exists only in the cache. In my results the hot-set trace sends about 20% of accesses to memory under write-through (0.1975 per access) and almost none under write-back (0.0006).

**8. Write-allocate vs no-write-allocate?**
On a write miss, write-allocate brings the block in; no-write-allocate writes around the cache. Write-allocate pays off if the data is touched again; no-write-allocate suits write-once streams. Common pairings are write-back + write-allocate and write-through + no-write-allocate. In my random/strided results no-write-allocate lowered memory transactions per access (1.14 to 0.95) because it skips the fetch.

**9. How does block size affect hit rate?**
Larger blocks bring in more neighboring data (spatial locality) but at a fixed capacity there are fewer lines, so there is more conflict and wasted fetch. Sequential improved from 0.75 (16 B) to 0.984 (256 B); the hot-set trace dropped from 0.9994 (64 B) to 0.573 (256 B).

**10. What is AMAT and what does your estimate leave out?**
Average memory access time = hit time + miss rate x miss penalty; with two levels the penalty is L2 hit time + L2 local miss rate x memory latency. My version uses illustrative latencies (4/12/200 cycles) and counts every access equally. It ignores write buffers, overlapped misses, prefetching and queueing, so it is a model-based estimate and not a measurement.

### C++ and implementation

**11. Why a flat `std::vector<CacheLine>` instead of `vector<vector<>>`, `std::list` or `unordered_map`?**
A flat vector is contiguous, so scanning a set of 1 to 8 ways is cache-friendly with no per-set allocation. `std::list` gives O(1) reordering but pointer chasing and node allocation make it slower for tiny sets. A hash map from tag to way would speed up very large fully associative caches (O(1) instead of O(lines)), but adds hashing cost and a second structure to keep consistent on eviction, so I did not need it. A fixed `std::array` does not fit because the sizes are runtime-configurable.

**12. Why is `CacheConfig` immutable and validated in the constructor?**
So an invalid configuration can never exist: any code holding a `CacheConfig` can rely on its invariants (power-of-two sizes, consistent set count and bit widths). Errors surface once, at construction, with a clear message.

**13. What is the time and space complexity?**
An access is O(ways); a miss adds O(ways) to pick a victim. Memory is O(lines). A run is O(trace length x ways). The trace is held in memory, 16 bytes per access.

**14. Why `enum class`, `std::optional`, and exceptions?**
`enum class` avoids implicit conversions and name clashes. `std::optional<double>` models "AMAT is undefined with zero accesses" without a magic number; `std::optional<CacheConfig>` models "no L2". Exceptions (`std::invalid_argument`) report invalid input at the point it is detected, and `main` maps them to exit codes.

**15. What undefined-behavior or overflow pitfalls did you handle?**
Shifting a 64-bit value by 64 is undefined, so the set-index mask is `numSets - 1` (zero when there is one set) and the largest shift is 63. The trace parser checks for 64-bit overflow before each nibble shift, and the CLI parser checks overflow when parsing numbers and `K/M/G` suffixes. I avoided `strtoull`, which accepts signs and silently saturates.

**16. How did you test it?**
Hand-calculated unit tests (each expected value is derived in a comment), a randomized differential test against a different naive implementation, and end-to-end tests of the executables. One practical check worth doing yourself before you claim it: deliberately break a line (for example make LRU not refresh on a hit) and confirm that tests fail.

**17. Why are some floating-point comparisons exact in tests and others use a tolerance?**
Values like 3/4 and 1/4 are exactly representable in binary, so exact comparison is safe. AMAT values like 149.333... use `CHECK_NEAR` with a tolerance.

### Design trade-offs

**18. Why separate `Cache`, `Simulation`, `Report` and `main`?**
So the simulation is testable without I/O, the same `simulate`/`runSimulation` code serves the CLI and the benchmark runner, and formatting changes cannot affect results. It is also why the benchmark can load a trace once and reuse it for every configuration.

**19. Why use a per-line logical-clock stamp for both policies?**
One field and one victim rule (smallest stamp) keep the code small and make the difference between LRU and FIFO a single line (refresh on hit or not). The cost is O(ways) to find the victim, which is fine for small associativity; real hardware uses approximations such as pseudo-LRU. If I needed more policies with their own per-set state, I would introduce a replacement-policy interface.

**20. How would you make it more realistic, and what are its limits?**
Add access sizes and real trace formats, a timing model with MSHRs and write buffers, inclusive/exclusive L2, prefetching, pseudo-LRU, and multi-core coherence. Current limits: functional only, one core, physical addresses, no access sizes, AMAT is an estimate, and synthetic traces are illustrations rather than representative workloads.

## Questions to ask yourself about the results

- Why is the `sequential` hit rate exactly 0.9375? (64 B blocks, 4 B steps: 1 miss + 15 hits per block.)
- Why does `random` hit about 0.0625 at 4 KiB? (capacity / range = 4 KiB / 64 KiB.)
- Why does `strided` only improve at 32 KiB in the capacity sweep? (More sets spread the 4096-byte-apart addresses over several sets.)
- Why do LRU and FIFO look identical in the sweeps? (These synthetic traces have no recency structure that separates them; the hand-built test sequences do.)

## Resume-ready description (three bullets)

- Designed and implemented a configurable CPU cache simulator in C++17 (direct-mapped to fully associative, LRU/FIFO, write-back/write-through, write-allocate/no-write-allocate, optional L2 with a model-based AMAT estimate) with trace parsing, input validation and CSV/text reporting.
- Verified it with 20 CTest tests: hand-calculated cases, a randomized differential test against an independent reference model, and end-to-end tests of the command-line tools.
- Built a benchmark runner, a deterministic synthetic trace generator and pandas/Matplotlib scripts to study how associativity, capacity, block size, write policy and L2 size affect hit rate, memory traffic and estimated AMAT.

## Honesty notes

- It is a functional CPU-cache model. Do not say it measures real hardware or predicts GPU performance; AMAT and latencies are model-based estimates with illustrative inputs.
- Say "synthetic traces" when quoting results; they illustrate behavior and are not representative workloads.
- It does not classify misses, model timing, or implement coherence. Say so if asked.
- Only claim what you can explain line by line. If you did not write or run something yourself, do it before the interview.