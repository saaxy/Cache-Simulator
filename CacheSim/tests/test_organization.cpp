// Direct-mapped vs set-associative vs fully associative behavior.
// All caches use FIFO here; policy-specific behavior is tested in test_replacement.cpp.
// Expected results are hand-calculated in the comments.
#include "Cache.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

Cache makeCache(std::uint64_t capacity, std::uint64_t block, std::uint32_t ways) {
    return Cache(CacheConfig(capacity, block, ways, ReplacementPolicy::FIFO,
                             WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate));
}

Cache makeFullyAssociative(std::uint64_t capacity, std::uint64_t block) {
    return Cache(CacheConfig::fullyAssociative(capacity, block, ReplacementPolicy::FIFO,
                                               WritePolicy::WriteBack,
                                               WriteAllocatePolicy::WriteAllocate));
}

void read(Cache& cache, std::uint64_t address, AccessResult expected, int line) {
    testutil::checkEq(cache.access(AccessType::Read, address), expected,
                      "cache.access(Read, address)", "expected result", __FILE__, line);
}
#define READ(cache, addr, expected) read((cache), (addr), (expected), __LINE__)

// 256 B, 16 B blocks. Direct-mapped has 16 sets (index = bits 4-7), so
// 0x000 and 0x100 collide in set 0 and thrash: M M M M.
// 2-way has 8 sets (index = bits 4-6): 0x000 and 0x100 are still both set 0
// (tags 0 and 2) but the set has two ways, so: M M H H.
void testTwoWayRemovesConflict() {
    Cache direct = makeCache(256, 16, 1);
    READ(direct, 0x000, AccessResult::Miss);
    READ(direct, 0x100, AccessResult::Miss);
    READ(direct, 0x000, AccessResult::Miss);
    READ(direct, 0x100, AccessResult::Miss);
    CHECK_EQ(direct.statistics().hits(), 0u);

    Cache twoWay = makeCache(256, 16, 2);
    READ(twoWay, 0x000, AccessResult::Miss);
    READ(twoWay, 0x100, AccessResult::Miss);
    READ(twoWay, 0x000, AccessResult::Hit);
    READ(twoWay, 0x100, AccessResult::Hit);
    CHECK_EQ(twoWay.statistics().hits(), 2u);
    CHECK_EQ(twoWay.statistics().evictions, 0u);
}

// 2-way, 8 sets. Set 0 holds addresses that are multiples of 128:
// 0x000 (A), 0x080 (B), 0x100 (C). Logical clock = access number.
//  1 R A miss  fills way 0, stamp 1
//  2 R B miss  fills way 1, stamp 2
//  3 R C miss  set full, smallest stamp is A -> evict A. evictions = 1
//  4 R B hit   (FIFO does not refresh B)
//  5 R A miss  smallest stamp is B (2) -> evict B even though just used. evictions = 2
//  6 R C hit
//  7 R B miss  smallest stamp is C (3) -> evict C. evictions = 3
// Totals: hits at 4 and 6 = 2; misses = 5; evictions = 3.
void testFifoEvictionOrderInTwoWaySet() {
    Cache cache = makeCache(256, 16, 2);
    READ(cache, 0x000, AccessResult::Miss);
    READ(cache, 0x080, AccessResult::Miss);
    READ(cache, 0x100, AccessResult::Miss);
    CHECK_EQ(cache.statistics().evictions, 1u);
    READ(cache, 0x080, AccessResult::Hit);
    READ(cache, 0x000, AccessResult::Miss);
    CHECK_EQ(cache.statistics().evictions, 2u);
    READ(cache, 0x100, AccessResult::Hit);
    READ(cache, 0x080, AccessResult::Miss);
    CHECK_EQ(cache.statistics().hits(), 2u);
    CHECK_EQ(cache.statistics().misses(), 5u);
    CHECK_EQ(cache.statistics().evictions, 3u);
}

// Different sets do not interfere: filling set 0 completely, then touching set 1
// (0x010) causes no eviction and leaves set 0 intact.
void testSetsAreIndependent() {
    Cache cache = makeCache(256, 16, 2);
    READ(cache, 0x000, AccessResult::Miss);
    READ(cache, 0x080, AccessResult::Miss);
    READ(cache, 0x010, AccessResult::Miss);
    CHECK_EQ(cache.statistics().evictions, 0u);
    READ(cache, 0x000, AccessResult::Hit);
    READ(cache, 0x080, AccessResult::Hit);
}

// 4-way, 256 B, 16 B blocks: 4 sets (index = bits 4-5). Set 0 holds multiples
// of 64. Five distinct blocks (0x000..0x100) exceed the 4 ways:
//   five misses, the fifth evicts the oldest (0x000): evictions = 1.
//   R 0x040 hits (still present); R 0x000 misses (was evicted).
void testFourWayCapacityOfOneSet() {
    Cache cache = makeCache(256, 16, 4);
    READ(cache, 0x000, AccessResult::Miss);
    READ(cache, 0x040, AccessResult::Miss);
    READ(cache, 0x080, AccessResult::Miss);
    READ(cache, 0x0C0, AccessResult::Miss);
    CHECK_EQ(cache.statistics().evictions, 0u);
    READ(cache, 0x100, AccessResult::Miss);
    CHECK_EQ(cache.statistics().evictions, 1u);
    READ(cache, 0x040, AccessResult::Hit);
    READ(cache, 0x000, AccessResult::Miss);
}

// Fully associative, 64 B / 16 B = 4 lines in one set. 0x000, 0x040, 0x080, 0x0C0
// all collide in a 4-set direct-mapped cache, but here they coexist:
//   four misses, then four hits, no evictions.
// A fifth block (0x100) evicts the oldest (0x000): evictions = 1.
// That is a capacity miss, since 5 blocks cannot fit in 4 lines.
void testFullyAssociativeHasNoConflicts() {
    Cache cache = makeFullyAssociative(64, 16);
    const std::uint64_t addresses[] = {0x000, 0x040, 0x080, 0x0C0};
    for (std::uint64_t a : addresses) READ(cache, a, AccessResult::Miss);
    for (std::uint64_t a : addresses) READ(cache, a, AccessResult::Hit);
    CHECK_EQ(cache.statistics().evictions, 0u);
    READ(cache, 0x100, AccessResult::Miss);
    CHECK_EQ(cache.statistics().evictions, 1u);
    READ(cache, 0x000, AccessResult::Miss);
    READ(cache, 0x0C0, AccessResult::Hit);
}

// Every organization from the project spec constructs and runs.
void testAllOrganizationsRun() {
    const std::uint32_t waysList[] = {1, 2, 4, 8};
    for (std::uint32_t ways : waysList) {
        Cache cache = makeCache(1024, 16, ways);
        READ(cache, 0x1234, AccessResult::Miss);
        READ(cache, 0x1234, AccessResult::Hit);
    }
    Cache full = makeFullyAssociative(1024, 16);
    READ(full, 0x1234, AccessResult::Miss);
    READ(full, 0x1234, AccessResult::Hit);
}

}  // namespace

int main() {
    testTwoWayRemovesConflict();
    testFifoEvictionOrderInTwoWaySet();
    testSetsAreIndependent();
    testFourWayCapacityOfOneSet();
    testFullyAssociativeHasNoConflicts();
    testAllOrganizationsRun();
    return testutil::finish("test_organization");
}
