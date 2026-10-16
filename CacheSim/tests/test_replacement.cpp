// LRU vs FIFO behavior. Every expected result is hand-calculated in the comments.
// Geometry for the 2-way tests: 256 B, 16 B blocks, 2 ways -> 8 sets.
// Set 0 holds the multiples of 128, so A = 0x000, B = 0x080, C = 0x100 all
// compete for the same two ways.
#include "Cache.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

constexpr std::uint64_t A = 0x000;
constexpr std::uint64_t B = 0x080;
constexpr std::uint64_t C = 0x100;

Cache makeCache(ReplacementPolicy policy, std::uint64_t capacity, std::uint64_t block,
                std::uint32_t ways) {
    return Cache(CacheConfig(capacity, block, ways, policy, WritePolicy::WriteBack,
                             WriteAllocatePolicy::WriteAllocate));
}

Cache makeTwoWay(ReplacementPolicy policy) { return makeCache(policy, 256, 16, 2); }

Cache makeFullyAssociative(ReplacementPolicy policy) {
    return Cache(CacheConfig::fullyAssociative(64, 16, policy, WritePolicy::WriteBack,
                                               WriteAllocatePolicy::WriteAllocate));
}

void read(Cache& cache, std::uint64_t address, AccessResult expected, int line) {
    testutil::checkEq(cache.access(AccessType::Read, address), expected,
                      "cache.access(Read, address)", "expected result", __FILE__, line);
}
#define READ(cache, addr, expected) read((cache), (addr), (expected), __LINE__)

constexpr AccessResult H = AccessResult::Hit;
constexpr AccessResult M = AccessResult::Miss;

// Sequence A B A C A (2-way, one set). A is "hot".
// LRU (stamp = last use):
//   1 A miss {A:1}   2 B miss {A:1,B:2}   3 A hit {A:3,B:2}
//   4 C miss: smallest stamp is B -> evict B {A:3,C:4}   5 A hit
//   => M M H M H: 2 hits, 1 eviction.
// FIFO (stamp = insertion):
//   1 A miss {A:1}   2 B miss {A:1,B:2}   3 A hit (no refresh)
//   4 C miss: smallest stamp is A -> evict A {C:4,B:2}
//   5 A miss: smallest stamp is B -> evict B
//   => M M H M M: 1 hit, 2 evictions.
void testLruKeepsHotBlock() {
    Cache lru = makeTwoWay(ReplacementPolicy::LRU);
    READ(lru, A, M); READ(lru, B, M); READ(lru, A, H); READ(lru, C, M); READ(lru, A, H);
    CHECK_EQ(lru.statistics().hits(), 2u);
    CHECK_EQ(lru.statistics().evictions, 1u);

    Cache fifo = makeTwoWay(ReplacementPolicy::FIFO);
    READ(fifo, A, M); READ(fifo, B, M); READ(fifo, A, H); READ(fifo, C, M); READ(fifo, A, M);
    CHECK_EQ(fifo.statistics().hits(), 1u);
    CHECK_EQ(fifo.statistics().evictions, 2u);
}

// Sequence A B C B A C B (the Stage 4 FIFO example). FIFO: M M M H M H M,
// 2 hits, 3 evictions (worked out in test_organization.cpp).
// LRU:
//   1 A miss {A:1}   2 B miss {A:1,B:2}   3 C miss: evict A {C:3,B:2}
//   4 B hit, B:=4 {C:3,B:4}   5 A miss: evict C (smallest, 3) {A:5,B:4}
//   6 C miss: evict B (4) {A:5,C:6}   7 B miss: evict A (5)
//   => M M M H M M M: 1 hit, 6 misses, 4 evictions.
// Neither policy is better on every sequence: here FIFO happens to win.
void testNeitherPolicyAlwaysWins() {
    Cache lru = makeTwoWay(ReplacementPolicy::LRU);
    READ(lru, A, M); READ(lru, B, M); READ(lru, C, M); READ(lru, B, H);
    READ(lru, A, M); READ(lru, C, M); READ(lru, B, M);
    CHECK_EQ(lru.statistics().hits(), 1u);
    CHECK_EQ(lru.statistics().misses(), 6u);
    CHECK_EQ(lru.statistics().evictions, 4u);

    Cache fifo = makeTwoWay(ReplacementPolicy::FIFO);
    READ(fifo, A, M); READ(fifo, B, M); READ(fifo, C, M); READ(fifo, B, H);
    READ(fifo, A, M); READ(fifo, C, H); READ(fifo, B, M);
    CHECK_EQ(fifo.statistics().hits(), 2u);
    CHECK_EQ(fifo.statistics().misses(), 5u);
    CHECK_EQ(fifo.statistics().evictions, 3u);
}

// Fully associative, 4 lines (64 B / 16 B). Blocks a..e = 0x00,0x10,...,0x40.
// Fill a b c d (stamps 1-4), hit a (access 5), then miss on e (access 6).
// LRU: stamps {a:5,b:2,c:3,d:4} -> evict b. Then R a hits (a survived); R b misses.
// FIFO: stamps {a:1,b:2,c:3,d:4} -> evict a. Then R a misses.
void testRecencyOrderWithFourWays() {
    const std::uint64_t a = 0x00, b = 0x10, c = 0x20, d = 0x30, e = 0x40;

    Cache lru = makeFullyAssociative(ReplacementPolicy::LRU);
    READ(lru, a, M); READ(lru, b, M); READ(lru, c, M); READ(lru, d, M);
    READ(lru, a, H);
    READ(lru, e, M);
    CHECK_EQ(lru.statistics().evictions, 1u);
    READ(lru, a, H);
    READ(lru, b, M);

    Cache fifo = makeFullyAssociative(ReplacementPolicy::FIFO);
    READ(fifo, a, M); READ(fifo, b, M); READ(fifo, c, M); READ(fifo, d, M);
    READ(fifo, a, H);
    READ(fifo, e, M);
    CHECK_EQ(fifo.statistics().evictions, 1u);
    READ(fifo, a, M);
}

// Cyclic access to one more block than the set has ways: A B C A B C A B C in a
// 2-way set. Every access evicts the block needed next, so both policies miss
// every time: 9 misses, 0 hits, 2 compulsory fills + 7 evictions.
void testCyclicThrashingMissesUnderBothPolicies() {
    const std::uint64_t pattern[] = {A, B, C, A, B, C, A, B, C};
    const ReplacementPolicy policies[] = {ReplacementPolicy::LRU, ReplacementPolicy::FIFO};
    for (ReplacementPolicy policy : policies) {
        Cache cache = makeTwoWay(policy);
        for (std::uint64_t address : pattern) READ(cache, address, M);
        CHECK_EQ(cache.statistics().hits(), 0u);
        CHECK_EQ(cache.statistics().misses(), 9u);
        CHECK_EQ(cache.statistics().evictions, 7u);
    }
}

// With one way per set there is only one possible victim, so LRU and FIFO must
// give identical statistics on any sequence.
void testPolicyIsIrrelevantWhenDirectMapped() {
    const std::uint64_t pattern[] = {0x000, 0x100, 0x000, 0x010, 0x110, 0x010,
                                     0x100, 0x000, 0x000, 0x100};
    Cache lru = makeCache(ReplacementPolicy::LRU, 256, 16, 1);
    Cache fifo = makeCache(ReplacementPolicy::FIFO, 256, 16, 1);
    for (std::uint64_t address : pattern) {
        lru.access(AccessType::Read, address);
        fifo.access(AccessType::Read, address);
    }
    CHECK_EQ(lru.statistics().hits(), fifo.statistics().hits());
    CHECK_EQ(lru.statistics().misses(), fifo.statistics().misses());
    CHECK_EQ(lru.statistics().evictions, fifo.statistics().evictions);
}

// LRU is now accepted for multi-way caches (it was rejected before Stage 5).
void testLruAcceptedForAllOrganizations() {
    const std::uint32_t waysList[] = {1, 2, 4, 8};
    for (std::uint32_t ways : waysList) {
        Cache cache = makeCache(ReplacementPolicy::LRU, 1024, 16, ways);
        READ(cache, 0x1234, M);
        READ(cache, 0x1234, H);
    }
}

}  // namespace

int main() {
    testLruKeepsHotBlock();
    testNeitherPolicyAlwaysWins();
    testRecencyOrderWithFourWays();
    testCyclicThrashingMissesUnderBothPolicies();
    testPolicyIsIrrelevantWhenDirectMapped();
    testLruAcceptedForAllOrganizations();
    return testutil::finish("test_replacement");
}
