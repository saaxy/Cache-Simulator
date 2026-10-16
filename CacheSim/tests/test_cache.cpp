// Basic hit/miss behavior of a direct-mapped cache, with hand-calculated results.
#include "Cache.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

// 256 B direct-mapped, 16 B blocks: 16 sets, offset bits = 4, index bits = 4,
// tag = address >> 8. Writes are treated like reads in this stage.
Cache makeCache() {
    return Cache(CacheConfig(256, 16, 1, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                             WriteAllocatePolicy::WriteAllocate));
}

// A block never seen before must miss (compulsory), with no eviction because
// the line was empty.
void testCompulsoryMiss() {
    Cache cache = makeCache();
    CHECK_EQ(cache.access(AccessType::Read, 0x000), AccessResult::Miss);
    CHECK_EQ(cache.statistics().readMisses, 1u);
    CHECK_EQ(cache.statistics().evictions, 0u);
}

// 0x000 miss; 0x000 hit; 0x00F is the same 16-byte block (offset 15) so hit;
// 0x010 is the next block (set 1) so a compulsory miss.
void testRepeatAndSameBlockHits() {
    Cache cache = makeCache();
    CHECK_EQ(cache.access(AccessType::Read, 0x000), AccessResult::Miss);
    CHECK_EQ(cache.access(AccessType::Read, 0x000), AccessResult::Hit);
    CHECK_EQ(cache.access(AccessType::Read, 0x00F), AccessResult::Hit);
    CHECK_EQ(cache.access(AccessType::Read, 0x010), AccessResult::Miss);
    CHECK_EQ(cache.statistics().hits(), 2u);
    CHECK_EQ(cache.statistics().misses(), 2u);
}

// 0x000 and 0x100 both map to set 0 (blockAddress 0x0 and 0x10, both
// mod 16 = 0) with tags 0 and 1. Alternating them thrashes the single line:
//   R 0x000 miss (empty), R 0x100 miss (evicts), R 0x000 miss (evicts)
// => 3 misses, 0 hits, 2 evictions.
void testDirectMappedConflictMisses() {
    Cache cache = makeCache();
    CHECK_EQ(cache.access(AccessType::Read, 0x000), AccessResult::Miss);
    CHECK_EQ(cache.access(AccessType::Read, 0x100), AccessResult::Miss);
    CHECK_EQ(cache.access(AccessType::Read, 0x000), AccessResult::Miss);
    CHECK_EQ(cache.statistics().hits(), 0u);
    CHECK_EQ(cache.statistics().misses(), 3u);
    CHECK_EQ(cache.statistics().evictions, 2u);
}

// Reads and writes are counted separately:
//   W 0x20 miss, W 0x20 hit, R 0x20 hit.
void testReadWriteCounters() {
    Cache cache = makeCache();
    cache.access(AccessType::Write, 0x20);
    cache.access(AccessType::Write, 0x20);
    cache.access(AccessType::Read, 0x20);
    const Statistics& s = cache.statistics();
    CHECK_EQ(s.writeMisses, 1u);
    CHECK_EQ(s.writeHits, 1u);
    CHECK_EQ(s.readHits, 1u);
    CHECK_EQ(s.readMisses, 0u);
    CHECK_EQ(s.reads(), 1u);
    CHECK_EQ(s.writes(), 2u);
    CHECK_EQ(s.accesses(), 3u);
}

// 1 miss then 3 hits: hit rate 3/4 = 0.75, miss rate 1/4 = 0.25 (both exact
// in binary floating point).
void testRates() {
    Cache cache = makeCache();
    for (int i = 0; i < 4; ++i) cache.access(AccessType::Read, 0x40);
    CHECK_EQ(cache.statistics().hitRate(), 0.75);
    CHECK_EQ(cache.statistics().missRate(), 0.25);
}

// Zero accesses must not divide by zero.
void testZeroAccesses() {
    const Cache cache = makeCache();
    CHECK_EQ(cache.statistics().accesses(), 0u);
    CHECK_EQ(cache.statistics().hitRate(), 0.0);
    CHECK_EQ(cache.statistics().missRate(), 0.0);
}

}  // namespace

int main() {
    testCompulsoryMiss();
    testRepeatAndSameBlockHits();
    testDirectMappedConflictMisses();
    testReadWriteCounters();
    testRates();
    testZeroAccesses();
    return testutil::finish("test_cache");
}
