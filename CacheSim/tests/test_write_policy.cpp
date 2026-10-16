// Write-policy and memory-traffic behavior. Every expected value is
// hand-calculated in the comments.
//
// Direct-mapped geometry unless stated: 256 B, 16 B blocks -> 16 sets.
// X = 0x000 and Y = 0x100 conflict in set 0; Z = 0x010 is in set 1.
// "memR"/"memW" are memory reads (block fetches) and memory writes (write
// transactions); "dirty" is the number of dirty lines still resident at the end.
#include "Cache.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

constexpr std::uint64_t X = 0x000;
constexpr std::uint64_t Y = 0x100;

Cache makeCache(WritePolicy write, WriteAllocatePolicy alloc, std::uint32_t ways = 1,
                ReplacementPolicy replacement = ReplacementPolicy::LRU) {
    return Cache(CacheConfig(256, 16, ways, replacement, write, alloc));
}

void r(Cache& cache, std::uint64_t address) { cache.access(AccessType::Read, address); }
void w(Cache& cache, std::uint64_t address) { cache.access(AccessType::Write, address); }

struct Expected {
    std::uint64_t hits, misses, evictions, dirtyEvictions, memR, memW, dirty;
};

void expectStats(const Cache& cache, const Expected& e, int line) {
    const Statistics& s = cache.statistics();
    using testutil::checkEq;
    checkEq(s.hits(), e.hits, "hits", "expected hits", __FILE__, line);
    checkEq(s.misses(), e.misses, "misses", "expected misses", __FILE__, line);
    checkEq(s.evictions, e.evictions, "evictions", "expected evictions", __FILE__, line);
    checkEq(s.dirtyEvictions, e.dirtyEvictions, "dirtyEvictions", "expected dirtyEvictions",
            __FILE__, line);
    checkEq(s.memoryReads, e.memR, "memoryReads", "expected memR", __FILE__, line);
    checkEq(s.memoryWrites, e.memW, "memoryWrites", "expected memW", __FILE__, line);
    checkEq(cache.dirtyLineCount(), e.dirty, "dirtyLineCount", "expected dirty", __FILE__,
            line);
}
#define EXPECT_STATS(cache, ...) expectStats((cache), Expected{__VA_ARGS__}, __LINE__)

// Write-back + write-allocate: W X, W X, R Y.
//  1 W X miss: fetch (memR 1), line dirty, no memory write
//  2 W X hit : already dirty, still no memory write
//  3 R Y miss: victim X is dirty -> eviction + dirty eviction + memory write
//              (memW 1); fetch Y (memR 2); Y is clean
// hits 1, misses 2, evictions 1, dirtyEv 1, memR 2, memW 1, dirty resident 0.
void testWriteBackDefersMemoryWrite() {
    Cache cache = makeCache(WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate);
    w(cache, X);
    EXPECT_STATS(cache, 0, 1, 0, 0, 1, 0, 1);   // dirty line resident, memW still 0
    w(cache, X);
    r(cache, Y);
    EXPECT_STATS(cache, 1, 2, 1, 1, 2, 1, 0);
}

// Write-through + write-allocate: W X, W X, R Y.
//  1 W X miss: fetch (memR 1), write forwarded (memW 1), line stays clean
//  2 W X hit : write forwarded (memW 2)
//  3 R Y miss: victim X is clean -> eviction but no dirty eviction; memR 2
// hits 1, misses 2, evictions 1, dirtyEv 0, memR 2, memW 2, dirty 0.
void testWriteThroughForwardsEveryWrite() {
    Cache cache = makeCache(WritePolicy::WriteThrough, WriteAllocatePolicy::WriteAllocate);
    w(cache, X);
    w(cache, X);
    r(cache, Y);
    EXPECT_STATS(cache, 1, 2, 1, 0, 2, 2, 0);
}

// Write-back + no-write-allocate: W X, R X, W X.
//  1 W X miss: bypasses cache, memW 1, nothing filled (memR 0)
//  2 R X miss: fetch (memR 1), clean
//  3 W X hit : line becomes dirty, no memory write
// hits 1, misses 2, evictions 0, dirtyEv 0, memR 1, memW 1, dirty 1.
void testNoWriteAllocateWithWriteBack() {
    Cache cache = makeCache(WritePolicy::WriteBack, WriteAllocatePolicy::NoWriteAllocate);
    w(cache, X);
    r(cache, X);
    w(cache, X);
    EXPECT_STATS(cache, 1, 2, 0, 0, 1, 1, 1);
}

// Write-through + no-write-allocate: W X, W X, R X, W X.
//  1 W X miss: memW 1 (not cached)   2 W X miss again: memW 2
//  3 R X miss: fetch (memR 1)        4 W X hit: forwarded, memW 3
// hits 1, misses 3, evictions 0, dirtyEv 0, memR 1, memW 3, dirty 0.
void testNoWriteAllocateWithWriteThrough() {
    Cache cache = makeCache(WritePolicy::WriteThrough, WriteAllocatePolicy::NoWriteAllocate);
    w(cache, X);
    w(cache, X);
    r(cache, X);
    w(cache, X);
    EXPECT_STATS(cache, 1, 3, 0, 0, 1, 3, 0);
}

// Sequence R X, W Y, R X with X, Y conflicting.
// Write-back + write-allocate:
//  1 R X miss (memR 1)
//  2 W Y miss: allocate, evict clean X (evictions 1), memR 2, Y dirty
//  3 R X miss: evict dirty Y (evictions 2, dirtyEv 1, memW 1), memR 3
//  => hits 0, misses 3, evictions 2, dirtyEv 1, memR 3, memW 1, dirty 0
// Write-back + no-write-allocate:
//  1 R X miss (memR 1)
//  2 W Y miss: bypass, memW 1, X stays resident
//  3 R X hit
//  => hits 1, misses 2, evictions 0, dirtyEv 0, memR 1, memW 1, dirty 0
// No-write-allocate avoids the thrash here.
void testAllocationChangesConflictBehavior() {
    Cache allocate = makeCache(WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate);
    r(allocate, X);
    w(allocate, Y);
    r(allocate, X);
    EXPECT_STATS(allocate, 0, 3, 2, 1, 3, 1, 0);

    Cache bypass = makeCache(WritePolicy::WriteBack, WriteAllocatePolicy::NoWriteAllocate);
    r(bypass, X);
    w(bypass, Y);
    r(bypass, X);
    EXPECT_STATS(bypass, 1, 2, 0, 0, 1, 1, 0);
}

// A refilled line must start clean: W X (dirty), R Y (evicts dirty X, memW 1),
// R X (evicts clean Y; X is refetched clean).
// dirtyEvictions stays 1, memW 1, memR 3, dirty resident 0.
void testRefilledLineStartsClean() {
    Cache cache = makeCache(WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate);
    w(cache, X);
    r(cache, Y);
    r(cache, X);
    EXPECT_STATS(cache, 0, 3, 2, 1, 3, 1, 0);
}

// The dirty bit travels with its line and the right line is written back.
// 2-way LRU, write-back/allocate, A=0x000, B=0x080, C=0x100 share set 0:
//  1 W A miss, dirty, memR 1      2 W B miss, dirty, memR 2
//  3 R A hit (A is now most recent)
//  4 R C miss: LRU victim is B (dirty) -> dirtyEv 1, memW 1, memR 3
// A stays resident and dirty: dirty resident 1 (C is clean).
// hits 1, misses 3, evictions 1, dirtyEv 1, memR 3, memW 1, dirty 1.
void testDirtyBitFollowsLineUnderLru() {
    Cache cache = makeCache(WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate, 2);
    w(cache, 0x000);
    w(cache, 0x080);
    r(cache, 0x000);
    r(cache, 0x100);
    EXPECT_STATS(cache, 1, 3, 1, 1, 3, 1, 1);
}

// A read-only trace never writes memory or dirties a line, for any policy.
// R X, R Y, R X, R Z (direct-mapped): X miss (fill), Y miss (evicts X),
// X miss (evicts Y), Z miss (set 1, empty, no eviction).
// misses 4, evictions 2, memR 4, memW 0, dirty 0.
void testReadsNeverWriteMemory() {
    const WritePolicy writes[] = {WritePolicy::WriteBack, WritePolicy::WriteThrough};
    const WriteAllocatePolicy allocs[] = {WriteAllocatePolicy::WriteAllocate,
                                          WriteAllocatePolicy::NoWriteAllocate};
    for (WritePolicy wp : writes) {
        for (WriteAllocatePolicy ap : allocs) {
            Cache cache = makeCache(wp, ap);
            r(cache, X);
            r(cache, Y);
            r(cache, X);
            r(cache, 0x010);
            EXPECT_STATS(cache, 0, 4, 2, 0, 4, 0, 0);
        }
    }
}

// Read/write counters still split correctly under the new write paths:
// W X (miss), R X (hit), W X (hit) under write-back/allocate.
void testReadWriteSplit() {
    Cache cache = makeCache(WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate);
    w(cache, X);
    r(cache, X);
    w(cache, X);
    CHECK_EQ(cache.statistics().writeMisses, 1u);
    CHECK_EQ(cache.statistics().writeHits, 1u);
    CHECK_EQ(cache.statistics().readHits, 1u);
    CHECK_EQ(cache.statistics().readMisses, 0u);
}

}  // namespace

int main() {
    testWriteBackDefersMemoryWrite();
    testWriteThroughForwardsEveryWrite();
    testNoWriteAllocateWithWriteBack();
    testNoWriteAllocateWithWriteThrough();
    testAllocationChangesConflictBehavior();
    testRefilledLineStartsClean();
    testDirtyBitFollowsLineUnderLru();
    testReadsNeverWriteMemory();
    testReadWriteSplit();
    return testutil::finish("test_write_policy");
}
