#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CacheConfig.h"
#include "Latency.h"

namespace cachesim {

// Settings gathered from the command line. Defaults describe a typical
// 32 KiB, 4-way, 64 B-block L1 data cache with no L2.
struct Options {
    std::string tracePath;
    std::uint64_t capacityBytes = 32 * 1024;
    std::uint64_t blockSizeBytes = 64;
    std::uint32_t ways = 4;
    bool fullyAssociative = false;  // --assoc full (ways is ignored)
    ReplacementPolicy replacement = ReplacementPolicy::LRU;
    WritePolicy writePolicy = WritePolicy::WriteBack;
    WriteAllocatePolicy writeAllocate = WriteAllocatePolicy::WriteAllocate;

    // Optional second level; enabled by --l2-size. L2 is write-back + write-allocate.
    bool hasL2 = false;
    std::uint64_t l2CapacityBytes = 0;
    std::uint64_t l2BlockSizeBytes = 0;  // 0 means "same as L1"
    std::uint32_t l2Ways = 8;
    bool l2FullyAssociative = false;
    ReplacementPolicy l2Replacement = ReplacementPolicy::LRU;

    LatencyModel latency;  // for the AMAT estimate only

    bool csv = false;
    bool csvHeader = true;
    bool help = false;
};

// Parses arguments (excluding the program name). Accepts "--opt value" and
// "--opt=value". Throws std::invalid_argument with a clear message for unknown
// options, missing or malformed values, L2 tuning options without --l2-size,
// or a missing --trace (unless --help).
Options parseArguments(const std::vector<std::string>& args);

// Builds a validated L1 CacheConfig; throws std::invalid_argument for an
// invalid geometry (e.g. non-power-of-two sizes).
CacheConfig makeCacheConfig(const Options& options);

// Builds the L2 config, or std::nullopt when --l2-size was not given.
std::optional<CacheConfig> makeL2Config(const Options& options);

std::string usageText();

// Shared value parsers (also used by the benchmark and trace-generator tools).
// parseSizeValue accepts an optional K/M/G suffix (powers of 1024). Both throw
// std::invalid_argument naming `option` on bad input or overflow.
std::uint64_t parseSizeValue(const std::string& text, const std::string& option);
std::uint64_t parseUnsignedValue(const std::string& text, const std::string& option);

}  // namespace cachesim
