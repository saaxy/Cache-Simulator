#pragma once

#include <cstdint>

#include "Access.h"

namespace cachesim {

// Raw counters plus derived quantities. Rates are fractions in [0, 1];
// reporting code is responsible for formatting them as percentages.
//
// Memory traffic is counted in transactions, not bytes (the trace has no
// access sizes):
//   memoryReads  = blocks fetched from memory (one per cache fill).
//   memoryWrites = write transactions sent to memory: one per write under
//                  write-through, or a write that bypasses the cache
//                  (no-write-allocate miss) -- each is a small store -- and
//                  one whole-block write per dirty eviction under write-back.
// A write-through store and a write-back block are different sizes, so compare
// memoryWrites across write policies with that in mind.
struct Statistics {
    std::uint64_t readHits = 0;
    std::uint64_t readMisses = 0;
    std::uint64_t writeHits = 0;
    std::uint64_t writeMisses = 0;
    std::uint64_t evictions = 0;       // valid lines replaced
    std::uint64_t dirtyEvictions = 0;  // subset of evictions that held dirty data
    std::uint64_t memoryReads = 0;
    std::uint64_t memoryWrites = 0;

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
