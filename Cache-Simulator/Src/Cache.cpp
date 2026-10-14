#include "Cache.h"

#include <cstddef>
#include <stdexcept>

namespace cachesim {

Cache::Cache(const CacheConfig& config)
    : config_(config), lines_(static_cast<std::size_t>(config.numLines())) {
    if (config.ways() != 1) {
        throw std::invalid_argument(
            "this build supports direct-mapped caches only (associativity must be 1)");
    }
}

AccessResult Cache::access(AccessType type, std::uint64_t address) {
    const AddressParts parts = config_.split(address);
    const std::uint32_t ways = config_.ways();
    CacheLine* set = &lines_[static_cast<std::size_t>(parts.setIndex) * ways];

    // Hit check: scan every way of the indexed set.
    for (std::uint32_t way = 0; way < ways; ++way) {
        if (set[way].valid && set[way].tag == parts.tag) {
            stats_.recordAccess(type, AccessResult::Hit);
            return AccessResult::Hit;
        }
    }

    // Miss: prefer an empty way; otherwise way 0. With ways == 1 (enforced in
    // the constructor) way 0 is the only candidate, so this is exact. Real
    // replacement policies replace the fallback in Stages 4-5.
    std::uint32_t victimWay = 0;
    for (std::uint32_t way = 0; way < ways; ++way) {
        if (!set[way].valid) {
            victimWay = way;
            break;
        }
    }

    CacheLine& victim = set[victimWay];
    if (victim.valid) {
        ++stats_.evictions;
    }
    victim.valid = true;
    victim.dirty = false;
    victim.tag = parts.tag;

    stats_.recordAccess(type, AccessResult::Miss);
    return AccessResult::Miss;
}

}  // namespace cachesim