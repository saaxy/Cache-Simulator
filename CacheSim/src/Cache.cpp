#include "Cache.h"

#include <algorithm>
#include <cstddef>

namespace cachesim {

namespace {

// Way to fill on a miss: an empty way if one exists, otherwise the way with
// the smallest stamp (oldest insertion for FIFO, least recently used for LRU).
// Stamps are unique (the clock ticks on every access and a stamp is written at
// most once per access), so there are no ties.
std::uint32_t chooseVictimWay(const CacheLine* set, std::uint32_t ways) {
    std::uint32_t victim = 0;
    for (std::uint32_t way = 0; way < ways; ++way) {
        if (!set[way].valid) {
            return way;
        }
        if (set[way].stamp < set[victim].stamp) {
            victim = way;
        }
    }
    return victim;
}

}  // namespace

Cache::Cache(const CacheConfig& config)
    : config_(config), lines_(static_cast<std::size_t>(config.numLines())) {}

std::uint64_t Cache::dirtyLineCount() const {
    return static_cast<std::uint64_t>(
        std::count_if(lines_.begin(), lines_.end(),
                      [](const CacheLine& line) { return line.valid && line.dirty; }));
}

bool Cache::applyWrite(CacheLine& line) {
    if (config_.writePolicy() == WritePolicy::WriteBack) {
        line.dirty = true;        // defer the memory write until this line is evicted
        return false;
    }
    ++stats_.memoryWrites;        // write-through: forward every write to memory
    return true;
}

AccessResult Cache::access(AccessType type, std::uint64_t address) {
    return accessDetailed(type, address).result;
}

AccessOutcome Cache::accessDetailed(AccessType type, std::uint64_t address) {
    ++clock_;
    AccessOutcome outcome;
    const bool isWrite = (type == AccessType::Write);
    const AddressParts parts = config_.split(address);
    const std::uint32_t ways = config_.ways();
    CacheLine* set = &lines_[static_cast<std::size_t>(parts.setIndex) * ways];

    // Hit check: scan every way of the indexed set. Under LRU a hit refreshes
    // the stamp (most recently used = largest stamp); under FIFO it does not.
    for (std::uint32_t way = 0; way < ways; ++way) {
        CacheLine& line = set[way];
        if (line.valid && line.tag == parts.tag) {
            if (config_.replacement() == ReplacementPolicy::LRU) {
                line.stamp = clock_;
            }
            if (isWrite) {
                outcome.writeForwarded = applyWrite(line);
            }
            outcome.result = AccessResult::Hit;
            stats_.recordAccess(type, AccessResult::Hit);
            return outcome;
        }
    }

    // Write miss without allocation: the write goes straight to the next level
    // and the cache contents (and replacement state) are untouched.
    if (isWrite && config_.writeAllocate() == WriteAllocatePolicy::NoWriteAllocate) {
        ++stats_.memoryWrites;
        outcome.writeForwarded = true;
        stats_.recordAccess(type, AccessResult::Miss);
        return outcome;
    }

    // Miss with allocation: choose a way, evict its block if it holds one
    // (writing it back if dirty), then fetch and fill the new block.
    CacheLine& victim = set[chooseVictimWay(set, ways)];
    if (victim.valid) {
        ++stats_.evictions;
        if (victim.dirty) {
            ++stats_.dirtyEvictions;
            ++stats_.memoryWrites;
            outcome.dirtyEviction = true;
            outcome.evictedBlockAddress =
                ((victim.tag << config_.indexBits()) | parts.setIndex) << config_.offsetBits();
        }
    }
    victim.valid = true;
    victim.dirty = false;  // a freshly fetched block is clean
    victim.tag = parts.tag;
    victim.stamp = clock_;
    ++stats_.memoryReads;
    outcome.blockFetched = true;

    if (isWrite) {
        outcome.writeForwarded = applyWrite(victim);  // complete the write on the new line
    }

    stats_.recordAccess(type, AccessResult::Miss);
    return outcome;
}

}  // namespace cachesim
