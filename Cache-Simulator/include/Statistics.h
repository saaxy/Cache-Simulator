#pragma once

#include <cstdint>

#include "Access.h"

namespace cachesim {

// Raw counters plus derived quantities. Rates are fractions in [0, 1];
// reporting code is responsible for formatting them as percentages.
struct Statistics {
    std::uint64_t readHits = 0;
    std::uint64_t readMisses = 0;
    std::uint64_t writeHits = 0;
    std::uint64_t writeMisses = 0;
    std::uint64_t evictions = 0;

    void recordAccess(AccessType type, AccessResult result);

    std::uint64_t reads() const { return readHits + readMisses; }
    std::uint64_t writes() const { return writeHits + writeMisses; }
    std::uint64_t hits() const { return readHits + writeHits; }
    std::uint64_t misses() const { return readMisses + writeMisses; }
    std::uint64_t accesses() const { return hits() + misses(); }

    // Both return 0.0 when there have been no accesses (never divide by zero).
    double hitRate() const;
    double missRate() const;
};

}  // namespace cachesim