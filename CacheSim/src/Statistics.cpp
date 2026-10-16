#include "Statistics.h"

namespace cachesim {

void Statistics::recordAccess(AccessType type, AccessResult result) {
    if (type == AccessType::Read) {
        if (result == AccessResult::Hit) ++readHits; else ++readMisses;
    } else {
        if (result == AccessResult::Hit) ++writeHits; else ++writeMisses;
    }
}

double Statistics::hitRate() const {
    const std::uint64_t total = accesses();
    return total == 0 ? 0.0 : static_cast<double>(hits()) / static_cast<double>(total);
}

double Statistics::missRate() const {
    const std::uint64_t total = accesses();
    return total == 0 ? 0.0 : static_cast<double>(misses()) / static_cast<double>(total);
}

}  // namespace cachesim
