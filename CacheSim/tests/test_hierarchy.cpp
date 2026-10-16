// Two-level (L1 + L2) behavior and the model-based AMAT estimate.
// Hand-calculated examples; geometry: L1 = 64 B direct-mapped, 16 B blocks
// (4 sets); L2 = 256 B 4-way, 16 B blocks (4 sets). 0x00 and 0x40 both map to
// L1 set 0 (index = bits 4-5), so they conflict in L1 but coexist in L2.
#include <random>
#include <vector>

#include "Cache.h"
#include "Simulation.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

CacheConfig makeL1(WritePolicy wp = WritePolicy::WriteBack,
                   WriteAllocatePolicy wa = WriteAllocatePolicy::WriteAllocate) {
    return CacheConfig(64, 16, 1, ReplacementPolicy::LRU, wp, wa);
}

CacheConfig makeL2() {
    return CacheConfig(256, 16, 4, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                       WriteAllocatePolicy::WriteAllocate);
}

constexpr AccessType R = AccessType::Read;
constexpr AccessType W = AccessType::Write;

// R 0x00, R 0x40, R 0x00, R 0x40.
// L1: all four miss (conflict), evictions 3. L2 gets 4 reads: 0x00 miss,
// 0x40 miss, 0x00 hit, 0x40 hit -> accesses 4, hits 2, misses 2, memory reads 2.
// AMAT = 4 + MR1(1.0) * (12 + MR2(0.5) * 200) = 116.
void testL2AbsorbsL1ConflictMisses() {
    const RunResult r = runSimulation(makeL1(), makeL2(), LatencyModel{},
                                      {{R, 0x00}, {R, 0x40}, {R, 0x00}, {R, 0x40}});
    CHECK_EQ(r.l1.stats.misses(), 4u);
    CHECK_EQ(r.l1.stats.evictions, 3u);
    CHECK_EQ(r.l2.has_value(), true);
    CHECK_EQ(r.l2->stats.accesses(), 4u);
    CHECK_EQ(r.l2->stats.hits(), 2u);
    CHECK_EQ(r.l2->stats.misses(), 2u);
    CHECK_EQ(r.l2->stats.memoryReads, 2u);
    CHECK_NEAR(*estimatedAmat(r), 116.0, 1e-9);
}

// W 0x00, R 0x40 with a write-back L1.
//  1 W 0x00: L1 miss, fetch -> L2 read 0x00 (miss). L1 line dirty.
//  2 R 0x40: L1 miss, victim 0x00 is dirty -> L2 write of block 0x00 (hit,
//            L2 line becomes dirty), then fetch -> L2 read 0x40 (miss).
// L1: 2 misses, 1 dirty eviction. L2: 3 requests (read miss, write hit, read
// miss): hits 1, misses 2, memory reads 2, dirty lines resident 1.
// AMAT = 4 + 1.0 * (12 + (2/3) * 200) = 149.3333...
void testDirtyEvictionBecomesL2Write() {
    const RunResult r = runSimulation(makeL1(), makeL2(), LatencyModel{}, {{W, 0x00}, {R, 0x40}});
    CHECK_EQ(r.l1.stats.misses(), 2u);
    CHECK_EQ(r.l1.stats.dirtyEvictions, 1u);
    CHECK_EQ(r.l2->stats.accesses(), 3u);
    CHECK_EQ(r.l2->stats.readMisses, 2u);
    CHECK_EQ(r.l2->stats.writeHits, 1u);
    CHECK_EQ(r.l2->stats.memoryReads, 2u);
    CHECK_EQ(r.l2->dirtyLinesResident, 1u);
    CHECK_NEAR(*estimatedAmat(r), 4.0 + 12.0 + (2.0 / 3.0) * 200.0, 1e-9);
}

// Write-through + no-write-allocate L1: W 0x00 bypasses L1 (nothing cached,
// L1 memory write 1) and arrives at L2 as a write; L2 is write-allocate, so it
// fetches the block (memory read 1) and holds it dirty.
void testForwardedWriteReachesL2() {
    const RunResult r = runSimulation(
        makeL1(WritePolicy::WriteThrough, WriteAllocatePolicy::NoWriteAllocate), makeL2(),
        LatencyModel{}, {{W, 0x00}});
    CHECK_EQ(r.l1.stats.misses(), 1u);
    CHECK_EQ(r.l1.stats.memoryWrites, 1u);
    CHECK_EQ(r.l1.stats.memoryReads, 0u);
    CHECK_EQ(r.l2->stats.accesses(), 1u);
    CHECK_EQ(r.l2->stats.writeMisses, 1u);
    CHECK_EQ(r.l2->stats.memoryReads, 1u);
    CHECK_EQ(r.l2->dirtyLinesResident, 1u);
}

// Write-through + write-allocate L1: W 0x00 misses, fetches (L2 read) and then
// forwards the store (L2 write). L2 sees 2 requests: read miss, write hit.
void testWriteThroughMissSendsFetchThenWrite() {
    const RunResult r = runSimulation(
        makeL1(WritePolicy::WriteThrough, WriteAllocatePolicy::WriteAllocate), makeL2(),
        LatencyModel{}, {{W, 0x00}});
    CHECK_EQ(r.l2->stats.accesses(), 2u);
    CHECK_EQ(r.l2->stats.readMisses, 1u);
    CHECK_EQ(r.l2->stats.writeHits, 1u);
}

// L1 hits never reach L2: R 0x00 five times -> L1 1 miss + 4 hits, L2 1 request.
// AMAT = 4 + 0.2 * (12 + 1.0 * 200) = 46.4
void testHitsDoNotTouchL2() {
    const std::vector<TraceEntry> trace(5, TraceEntry{R, 0x00});
    const RunResult r = runSimulation(makeL1(), makeL2(), LatencyModel{}, trace);
    CHECK_EQ(r.l1.stats.hits(), 4u);
    CHECK_EQ(r.l2->stats.accesses(), 1u);
    CHECK_NEAR(*estimatedAmat(r), 46.4, 1e-9);
}

// Single-level AMAT: 1 hit in 4 accesses -> 4 + 0.75 * 200 = 154. With custom
// latencies (L1 2, memory 100): 2 + 0.75 * 100 = 77.
void testSingleLevelAmat() {
    const CacheConfig dm(256, 16, 1, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                         WriteAllocatePolicy::WriteAllocate);
    const std::vector<TraceEntry> trace = {{R, 0x000}, {R, 0x000}, {W, 0x010}, {R, 0x100}};
    CHECK_NEAR(*estimatedAmat(runSimulation(dm, std::nullopt, LatencyModel{}, trace)), 154.0, 1e-9);
    CHECK_NEAR(*estimatedAmat(runSimulation(dm, std::nullopt, LatencyModel{2.0, 0.0, 100.0}, trace)),
               77.0, 1e-9);
}

// A perfect hit stream has AMAT equal to the L1 hit time only; no accesses
// means AMAT is undefined (nullopt), never a division by zero.
void testAmatEdgeCases() {
    const std::vector<TraceEntry> onlyHits(100, TraceEntry{R, 0x00});
    const RunResult hits = runSimulation(makeL1(), std::nullopt, LatencyModel{}, onlyHits);
    // 1 compulsory miss in 100 accesses -> 4 + 0.01 * 200 = 6
    CHECK_NEAR(*estimatedAmat(hits), 6.0, 1e-9);
    CHECK_EQ(estimatedAmat(runSimulation(makeL1(), std::nullopt, LatencyModel{}, {})).has_value(),
             false);
    CHECK_EQ(estimatedAmat(runSimulation(makeL1(), makeL2(), LatencyModel{}, {})).has_value(),
             false);
}

void testInvalidHierarchyIsRejected() {
    const CacheConfig smallBlockL2(256, 8, 4, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                                   WriteAllocatePolicy::WriteAllocate);
    CHECK_THROWS(runSimulation(makeL1(), smallBlockL2, LatencyModel{}, {}));   // L2 block < L1 block
    const CacheConfig tinyL2(32, 16, 1, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                             WriteAllocatePolicy::WriteAllocate);
    CHECK_THROWS(runSimulation(makeL1(), tinyL2, LatencyModel{}, {}));          // L2 smaller than L1
    CHECK_THROWS(runSimulation(makeL1(), std::nullopt, LatencyModel{-1.0, 12.0, 200.0}, {}));
}

// Property: with a read-only trace the L2 receives exactly one read per L1 miss
// (no writebacks, no forwarded writes). Randomized with a fixed seed.
void testL2RequestsEqualL1MissesForReads() {
    std::mt19937 rng(7);
    std::vector<TraceEntry> trace;
    for (int i = 0; i < 2000; ++i) {
        trace.push_back({R, static_cast<std::uint64_t>(rng() % 4096)});
    }
    const RunResult r = runSimulation(makeL1(), makeL2(), LatencyModel{}, trace);
    CHECK_EQ(r.l2->stats.accesses(), r.l1.stats.misses());
    CHECK_EQ(r.l2->stats.writes(), 0u);
}

// accessDetailed reports the evicted block's byte address correctly:
// W 0x4C (block 0x40, dirty), then R 0x04 (block 0x00, same L1 set) evicts it.
void testEvictedBlockAddress() {
    Cache l1(makeL1());
    l1.accessDetailed(W, 0x4C);
    const AccessOutcome o = l1.accessDetailed(R, 0x04);
    CHECK_EQ(o.dirtyEviction, true);
    CHECK_EQ(o.evictedBlockAddress, 0x40u);
    CHECK_EQ(o.blockFetched, true);
    CHECK_EQ(o.writeForwarded, false);
}

}  // namespace

int main() {
    testL2AbsorbsL1ConflictMisses();
    testDirtyEvictionBecomesL2Write();
    testForwardedWriteReachesL2();
    testWriteThroughMissSendsFetchThenWrite();
    testHitsDoNotTouchL2();
    testSingleLevelAmat();
    testAmatEdgeCases();
    testInvalidHierarchyIsRejected();
    testL2RequestsEqualL1MissesForReads();
    testEvictedBlockAddress();
    return testutil::finish("test_hierarchy");
}
