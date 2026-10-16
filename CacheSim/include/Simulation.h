#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Access.h"
#include "CacheConfig.h"
#include "Latency.h"
#include "Statistics.h"

namespace cachesim {

// Everything a report needs about one cache level.
struct SimulationResult {
    CacheConfig config;
    Statistics stats;
    std::uint64_t dirtyLinesResident;  // dirty lines left in the cache (not flushed)
};

// Runs a whole trace through a fresh single cache with the given configuration.
// The trace is taken by const reference so one loaded trace can be simulated
// under many configurations (the benchmark runner does exactly that).
SimulationResult simulate(const CacheConfig& config, const std::vector<TraceEntry>& trace);

// Result of a run with an optional second level.
struct RunResult {
    SimulationResult l1;
    std::optional<SimulationResult> l2;  // present only for a two-level run
    LatencyModel latency;
};

// Two-level hierarchy assumptions (also documented in the README):
//  - L2 is non-inclusive, non-exclusive: it is not forced to contain L1's blocks.
//  - L2 is touched only by traffic that leaves L1: a block fetch becomes an L2
//    read, a dirty-victim writeback becomes an L2 write (of that block's
//    address), and a forwarded store (write-through, or a no-write-allocate
//    miss) becomes an L2 write. Writeback is issued before the fetch.
//  - L2 is write-back + write-allocate; its replacement policy is configurable.
//  - Requires L2 block size >= L1 block size and L2 capacity >= L1 capacity.
// Throws std::invalid_argument if the hierarchy or the latencies are invalid.
// Pass std::nullopt for l2Config to simulate a single cache.
RunResult runSimulation(const CacheConfig& l1Config, const std::optional<CacheConfig>& l2Config,
                        const LatencyModel& latency, const std::vector<TraceEntry>& trace);

// Model-based AMAT estimate in cycles (NOT a hardware measurement):
//   single level: AMAT = L1 hit + L1 miss rate x memory
//   two levels:   AMAT = L1 hit + L1 miss rate x (L2 hit + L2 local miss rate x memory)
// Every access (read or write) is weighted equally; write buffers, write-through
// stalls, overlap, and queueing are not modeled. The L2 local miss rate is
// measured over all requests L2 receives (including writebacks). Returns
// std::nullopt when the trace had no accesses.
std::optional<double> estimatedAmat(const RunResult& result);

}  // namespace cachesim
