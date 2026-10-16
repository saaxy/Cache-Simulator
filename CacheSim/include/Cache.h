#pragma once

#include <cstdint>
#include <vector>

#include "Access.h"
#include "CacheConfig.h"
#include "CacheLine.h"
#include "Statistics.h"

namespace cachesim {

// What one access did at the boundary to the next level (L2 or memory).
// A multi-level driver uses this to forward traffic to the next cache.
struct AccessOutcome {
    AccessResult result = AccessResult::Miss;
    bool blockFetched = false;       // a whole block was requested from the next level
    bool writeForwarded = false;     // a store was sent to the next level (write-through
                                     // write, or a no-write-allocate write miss)
    bool dirtyEviction = false;      // a dirty victim block was written back
    std::uint64_t evictedBlockAddress = 0;  // byte address of that block's first byte
                                            // (meaningful only if dirtyEviction)
};

// Functional model of one cache: tracks which blocks are present and counts
// hits, misses, and memory traffic. It does not model data, timing, or
// hardware behavior.
//
// Organizations: direct-mapped, N-way set-associative, fully associative
// (they differ only in how many ways each set has).
//
// Replacement uses one per-line "stamp" (a logical-clock value). The victim is
// always the valid line with the smallest stamp; the policies differ only in
// when a stamp is written:
//   FIFO: only when the line is filled (stamp = insertion time).
//   LRU:  when the line is filled and on every hit (stamp = last-use time).
//
// Write handling (block-granularity model):
//   Read hit:   no memory traffic.
//   Read miss:  fetch the block (memoryReads++); evict a victim if the set is full.
//   Write hit:  write-back    -> mark the line dirty, no memory traffic yet.
//               write-through -> forward the write (memoryWrites++), line stays clean.
//   Write miss: write-allocate    -> fetch the block (memoryReads++), fill it, then
//                                    treat as a write hit.
//               no-write-allocate -> send the write to memory (memoryWrites++),
//                                    nothing is filled or evicted.
//   Evicting a dirty line (possible only under write-back) counts a dirty
//   eviction and one memory write.
// "Memory" here means "the next level": main memory for a single cache, or L2
// when this cache is L1 in a hierarchy (see Simulation.h).
// Assumptions: a write-allocate miss fetches the whole block first
// (fetch-on-write); there are no write buffers; dirty lines still resident when
// the trace ends are NOT flushed (see dirtyLineCount()).
class Cache {
public:
    explicit Cache(const CacheConfig& config);

    AccessResult access(AccessType type, std::uint64_t address);

    // Same as access() but also reports the traffic sent to the next level.
    AccessOutcome accessDetailed(AccessType type, std::uint64_t address);

    const CacheConfig& config() const { return config_; }
    const Statistics& statistics() const { return stats_; }

    // Number of dirty lines currently resident: write-back data that has not
    // reached memory yet.
    std::uint64_t dirtyLineCount() const;

private:
    // Applies a write to a line that holds the target block. Returns true if
    // the write was forwarded to the next level (write-through).
    bool applyWrite(CacheLine& line);

    CacheConfig config_;
    // Flat array of numSets * ways lines; set s occupies [s*ways, (s+1)*ways).
    std::vector<CacheLine> lines_;
    Statistics stats_;
    std::uint64_t clock_ = 0;  // logical time, incremented once per access
};

}  // namespace cachesim
