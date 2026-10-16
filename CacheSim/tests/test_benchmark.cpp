// Benchmark experiment building, CSV output, and argument parsing.
#include <algorithm>
#include <sstream>
#include <string>

#include "Benchmark.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

std::size_t countGroup(const std::vector<Experiment>& list, const std::string& group) {
    return static_cast<std::size_t>(std::count_if(
        list.begin(), list.end(), [&](const Experiment& e) { return e.group == group; }));
}

std::size_t columns(const std::string& line) {
    return static_cast<std::size_t>(std::count(line.begin(), line.end(), ',')) + 1;
}

// Base 4 KiB, 64 B blocks, 4-way (64 lines). Expected sweep sizes:
//   associativity 5 (1,2,4,8,full), replacement 2, capacity 7 (1K..64K),
//   block_size 5 (16..256), write_policy 4, l2_size 6 (no-L2 + 16K..256K) = 29.
void testStandardSweepSizes() {
    const std::vector<Experiment> e = buildExperiments(4096, 64, 4);
    CHECK_EQ(countGroup(e, "associativity"), 5u);
    CHECK_EQ(countGroup(e, "replacement"), 2u);
    CHECK_EQ(countGroup(e, "capacity"), 7u);
    CHECK_EQ(countGroup(e, "block_size"), 5u);
    CHECK_EQ(countGroup(e, "write_policy"), 4u);
    CHECK_EQ(countGroup(e, "l2_size"), 6u);
    CHECK_EQ(e.size(), 29u);
    CHECK_EQ(e.front().group, std::string("associativity"));
    CHECK_EQ(e.front().label, std::string("1-way"));
}

// Variants invalid for the base are skipped rather than crashing: a 256 B cache
// with 64 B blocks has only 4 lines, so 8-way is impossible, and every L2 of
// 16K+ is still fine.
void testInvalidVariantsAreSkipped() {
    const std::vector<Experiment> e = buildExperiments(256, 64, 4);
    CHECK_EQ(countGroup(e, "associativity"), 4u);  // 1,2,4,full (8-way skipped)
    CHECK_EQ(e.empty(), false);

    // A 64 KiB base L1 makes the 16K and 32K L2 variants invalid (L2 < L1):
    // what remains is the no-L2 baseline plus 64K, 128K and 256K.
    const std::vector<Experiment> big = buildExperiments(65536, 64, 4);
    CHECK_EQ(countGroup(big, "l2_size"), 4u);
}

void testInvalidBaseThrows() {
    CHECK_THROWS(buildExperiments(1000, 64, 4));   // capacity not a power of two
    CHECK_THROWS(buildExperiments(4096, 48, 4));
    CHECK_THROWS(buildExperiments(4096, 64, 3));
}

std::vector<NamedTrace> tinyTraces() {
    return {{"a.trace", generateTrace(defaultSpec(SyntheticKind::HotSet, 500, 1))},
            {"b.trace", generateTrace(defaultSpec(SyntheticKind::Strided, 500, 1))}};
}

// Every standard experiment must run on a real trace without throwing, and the
// output must be a rectangular CSV: header + (traces x experiments) rows, all
// with 2 + 38 columns.
void testRunBenchmarkProducesRectangularCsv() {
    const std::vector<Experiment> experiments = buildExperiments(4096, 64, 4);
    const std::vector<NamedTrace> traces = tinyTraces();
    std::ostringstream out;
    const std::size_t rows = runBenchmark(out, traces, experiments, LatencyModel{});
    CHECK_EQ(rows, experiments.size() * traces.size());

    std::istringstream in(out.str());
    std::string line;
    std::size_t lines = 0;
    while (std::getline(in, line)) {
        CHECK_EQ(columns(line), 40u);
        ++lines;
    }
    CHECK_EQ(lines, rows + 1);
    CHECK_EQ(out.str().rfind("experiment,variant,trace,capacity_bytes", 0), 0u);
}

// The "full" associativity variant of a 4 KiB, 64 B cache has 64 ways, 1 set.
void testFullyAssociativeVariant() {
    const std::vector<Experiment> e = buildExperiments(4096, 64, 4);
    const auto it = std::find_if(e.begin(), e.end(), [](const Experiment& x) {
        return x.group == "associativity" && x.label == "full";
    });
    CHECK_EQ(it != e.end(), true);
    CHECK_EQ(it->l1.ways(), 64u);
    CHECK_EQ(it->l1.numSets(), 1u);
}

// The strided trace (8 addresses 4096 B apart) collides in a 4 KiB 4-way cache
// but fits in a fully associative one: the CSV rows must show that contrast.
// 4-way, 16 sets: all 8 addresses map to set 0 -> cyclic access thrashes LRU:
// no hits at all. Fully associative: only the 8 compulsory misses.
void testStridedTraceShowsConflictMissesThenFullyAssociativeFixesThem() {
    const std::vector<TraceEntry> trace = generateTrace(defaultSpec(SyntheticKind::Strided, 800, 1));
    const std::vector<Experiment> e = buildExperiments(4096, 64, 4);
    for (const Experiment& x : e) {
        if (x.group != "associativity") continue;
        const RunResult r = runSimulation(x.l1, x.l2, LatencyModel{}, trace);
        if (x.label == "4-way" || x.label == "1-way" || x.label == "2-way") {
            CHECK_EQ(r.l1.stats.hits(), 0u);
        }
        if (x.label == "8-way" || x.label == "full") {
            CHECK_EQ(r.l1.stats.misses(), 8u);  // compulsory only
        }
    }
}

void testBenchmarkArguments() {
    const BenchmarkOptions o = parseBenchmarkArguments(
        {"--trace", "a.trace", "--trace", "b.trace", "--synthetic", "hotset", "--synthetic",
         "hotset", "--synthetic-count", "100", "--seed", "9", "--base-size", "8K", "--base-block",
         "32", "--base-assoc", "2", "--mem-latency", "300", "--out", "r.csv"});
    CHECK_EQ(o.tracePaths.size(), 2u);
    CHECK_EQ(o.syntheticKinds.size(), 1u);  // duplicate kind ignored
    CHECK_EQ(o.syntheticCount, 100u);
    CHECK_EQ(o.seed, 9u);
    CHECK_EQ(o.baseCapacity, 8192u);
    CHECK_EQ(o.baseBlock, 32u);
    CHECK_EQ(o.baseWays, 2u);
    CHECK_EQ(o.latency.memoryCycles, 300.0);
    CHECK_EQ(o.outPath, std::string("r.csv"));

    CHECK_EQ(parseBenchmarkArguments({"--synthetic", "all"}).syntheticKinds.size(), 4u);
    CHECK_EQ(parseBenchmarkArguments({"--help"}).help, true);
    CHECK_THROWS(parseBenchmarkArguments({}));                         // nothing to run
    CHECK_THROWS(parseBenchmarkArguments({"--synthetic", "zipf"}));
    CHECK_THROWS(parseBenchmarkArguments({"--trace"}));                // missing value
    CHECK_THROWS(parseBenchmarkArguments({"--trace", "a", "--base-assoc", "99999999999"}));
    CHECK_THROWS(parseBenchmarkArguments({"--trace", "a", "--bogus"}));
}

}  // namespace

int main() {
    testStandardSweepSizes();
    testInvalidVariantsAreSkipped();
    testInvalidBaseThrows();
    testRunBenchmarkProducesRectangularCsv();
    testFullyAssociativeVariant();
    testStridedTraceShowsConflictMissesThenFullyAssociativeFixesThem();
    testBenchmarkArguments();
    return testutil::finish("test_benchmark");
}
