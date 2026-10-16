#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "Access.h"

namespace cachesim {

// Synthetic access patterns for controlled experiments. Generation is fully
// deterministic: the same spec (including the seed) gives the same trace on
// every platform (std::mt19937_64 is specified by the C++ standard, and only
// raw generator output with '%' is used, never std::*_distribution).
enum class SyntheticKind {
    Sequential,  // walk an array of rangeBytes in strideBytes steps, wrapping around
    Random,      // uniformly random 4-byte-aligned addresses in [0, rangeBytes)
    HotSet,      // repeated random accesses to hotBlocks fixed 64-byte regions
    Strided,     // cycle through hotBlocks addresses spaced strideBytes apart
};

const char* toString(SyntheticKind kind);

// Accepts "sequential", "random", "hotset", "strided" (case-insensitive).
// Throws std::invalid_argument otherwise.
SyntheticKind parseSyntheticKind(const std::string& text);

std::vector<SyntheticKind> allSyntheticKinds();

struct SyntheticSpec {
    SyntheticKind kind = SyntheticKind::Sequential;
    std::uint64_t count = 50000;      // number of accesses
    std::uint64_t seed = 1;
    std::uint64_t rangeBytes = 65536; // address range [0, rangeBytes)
    std::uint64_t strideBytes = 4;    // Sequential / Strided step
    std::uint64_t hotBlocks = 32;     // HotSet / Strided: number of distinct addresses
    std::uint32_t writePercent = 20;  // each access is a write with this probability
};

// Defaults per kind (chosen so the effect of cache parameters is visible on a
// 4 KiB cache; see the README):
//   sequential: 16 KiB array, 4-byte stride (streaming with spatial locality)
//   random:     64 KiB range
//   hotset:     32 hot 64-byte regions (a 2 KiB working set) in a 64 KiB range
//   strided:    8 addresses 4096 bytes apart (they collide in small caches)
SyntheticSpec defaultSpec(SyntheticKind kind, std::uint64_t count = 50000,
                          std::uint64_t seed = 1);

// Throws std::invalid_argument for an unusable spec (zero stride/range/hotBlocks
// where needed, writePercent > 100, address overflow).
std::vector<TraceEntry> generateTrace(const SyntheticSpec& spec);

// Writes entries in the trace format: "R 0x0000ABCD" per line.
void writeTrace(std::ostream& out, const std::vector<TraceEntry>& trace);

// Command line of the cachesim_tracegen tool.
struct GeneratorOptions {
    SyntheticSpec spec;
    std::string outPath;  // empty = stdout
    bool help = false;
};

// Parses arguments (excluding the program name). --kind is required unless
// --help is given; other options override the per-kind defaults. Throws
// std::invalid_argument on bad input.
GeneratorOptions parseGeneratorArguments(const std::vector<std::string>& args);
std::string generatorUsageText();

}  // namespace cachesim
