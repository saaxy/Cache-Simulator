#pragma once

#include <cstdint>
#include <vector>

#include "Access.h"
#include "CacheConfig.h"
#include "CacheLine.h"
#include "Statistics.h"

namespace cachesim {

// Functional model of one cache: tracks which blocks are present and counts
// hits/misses. It does not model data, timing, or hardware behavior.
//
// Stage 3 limitation: only direct-mapped geometries (ways == 1) are accepted.
// Writes are treated like reads (a miss allocates); real write policies are
// added in Stage 6.
class Cache {
public:
    explicit Cache(const CacheConfig& config);

    AccessResult access(AccessType type, std::uint64_t address);

    const CacheConfig& config() const { return config_; }
    const Statistics& statistics() const { return stats_; }

private:
    CacheConfig config_;
    // Flat array of numSets * ways lines; set s occupies [s*ways, (s+1)*ways).
    std::vector<CacheLine> lines_;
    Statistics stats_;
};

}  // namespace cachesim