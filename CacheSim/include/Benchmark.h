#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "CacheConfig.h"
#include "Latency.h"
#include "Simulation.h"
#include "TraceGenerator.h"

namespace cachesim {

// A trace loaded (or generated) once and reused for every experiment.
struct NamedTrace {
    std::string name;
    std::vector<TraceEntry> entries;
};

// One cache configuration to evaluate. `group` is the parameter being swept
// (e.g. "associativity") and `label` names the value (e.g. "4-way").
struct Experiment {
    std::string group;
    std::string label;
    CacheConfig l1;
    std::optional<CacheConfig> l2;
};

// Builds the standard one-parameter-at-a-time sweeps around a base L1
// (baseWays-way, LRU, write-back, write-allocate):
//   associativity: 1, 2, 4, 8 ways and fully associative
//   replacement:   LRU vs FIFO
//   capacity:      1K .. 64K
//   block_size:    16 .. 256 bytes
//   write_policy:  write-back/through x write-allocate/no-write-allocate
//   l2_size:       no L2, then 8-way L2 of 16K .. 256K (block size = L1's)
// Variants that would be an invalid configuration for this base (e.g. 8 ways in
// a cache with 4 lines, an L2 smaller than L1) are skipped. Throws
// std::invalid_argument if the base configuration itself is invalid.
std::vector<Experiment> buildExperiments(std::uint64_t baseCapacity, std::uint64_t baseBlock,
                                         std::uint32_t baseWays);

// "experiment,variant," followed by the standard CSV columns (see Report.h).
std::string benchmarkCsvHeader();
std::string benchmarkCsvRow(const Experiment& experiment, const RunResult& result,
                            const std::string& traceName);

// Runs every experiment on every trace and writes the header plus one row per
// (trace, experiment) to `out`. Returns the number of data rows written.
std::size_t runBenchmark(std::ostream& out, const std::vector<NamedTrace>& traces,
                         const std::vector<Experiment>& experiments,
                         const LatencyModel& latency);

// Command line of the cachesim_bench tool.
struct BenchmarkOptions {
    std::vector<std::string> tracePaths;          // --trace FILE (repeatable)
    std::vector<SyntheticKind> syntheticKinds;    // --synthetic KIND|all (repeatable)
    std::string outPath;                          // --out FILE (empty = stdout)
    std::uint64_t baseCapacity = 4096;
    std::uint64_t baseBlock = 64;
    std::uint32_t baseWays = 4;
    std::uint64_t syntheticCount = 50000;
    std::uint64_t seed = 1;
    LatencyModel latency;
    bool help = false;
};

// Throws std::invalid_argument on bad input, or when neither --trace nor
// --synthetic is given (unless --help).
BenchmarkOptions parseBenchmarkArguments(const std::vector<std::string>& args);
std::string benchmarkUsageText();

}  // namespace cachesim
