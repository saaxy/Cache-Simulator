// Tests for CacheConfig geometry and validation. Expected values are hand-calculated.
#include "CacheConfig.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

CacheConfig make(std::uint64_t cap, std::uint64_t block, std::uint32_t ways) {
    return CacheConfig(cap, block, ways, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                       WriteAllocatePolicy::WriteAllocate);
}

// 32 KiB, 64 B blocks, 4-way:
//   lines = 32768 / 64 = 512; sets = 512 / 4 = 128
//   offset bits = log2(64) = 6; index bits = log2(128) = 7
void testFourWay() {
    const CacheConfig c = make(32768, 64, 4);
    CHECK_EQ(c.numLines(), 512u);
    CHECK_EQ(c.numSets(), 128u);
    CHECK_EQ(c.offsetBits(), 6u);
    CHECK_EQ(c.indexBits(), 7u);
}

// Direct-mapped: ways = 1, so sets = lines = 512, index bits = 9.
void testDirectMapped() {
    const CacheConfig c = make(32768, 64, 1);
    CHECK_EQ(c.numSets(), 512u);
    CHECK_EQ(c.indexBits(), 9u);
}

// Fully associative: one set, zero index bits, ways = lines = 512.
void testFullyAssociative() {
    const CacheConfig c = CacheConfig::fullyAssociative(
        32768, 64, ReplacementPolicy::FIFO, WritePolicy::WriteThrough,
        WriteAllocatePolicy::NoWriteAllocate);
    CHECK_EQ(c.ways(), 512u);
    CHECK_EQ(c.numSets(), 1u);
    CHECK_EQ(c.indexBits(), 0u);
    CHECK_EQ(c.replacement(), ReplacementPolicy::FIFO);
    CHECK_EQ(c.writePolicy(), WritePolicy::WriteThrough);
    CHECK_EQ(c.writeAllocate(), WriteAllocatePolicy::NoWriteAllocate);
}

// Smallest legal cache: capacity == block size, 1 line, 1 set, 0 index bits.
void testSingleLine() {
    const CacheConfig c = make(64, 64, 1);
    CHECK_EQ(c.numLines(), 1u);
    CHECK_EQ(c.numSets(), 1u);
    CHECK_EQ(c.indexBits(), 0u);
}

void testInvalidConfigs() {
    CHECK_THROWS(make(0, 64, 1));        // zero capacity
    CHECK_THROWS(make(1000, 64, 1));     // capacity not a power of two
    CHECK_THROWS(make(1024, 0, 1));      // zero block size
    CHECK_THROWS(make(1024, 48, 1));     // block size not a power of two
    CHECK_THROWS(make(64, 128, 1));      // block larger than cache
    CHECK_THROWS(make(1024, 64, 0));     // zero ways
    CHECK_THROWS(make(1024, 64, 3));     // ways not a power of two
    CHECK_THROWS(make(1024, 64, 32));    // 1024/64 = 16 lines, 32 ways is too many
    CHECK_THROWS(CacheConfig::fullyAssociative(
        1000, 64, ReplacementPolicy::LRU, WritePolicy::WriteBack,
        WriteAllocatePolicy::WriteAllocate));  // bad capacity propagates
}

}  // namespace

int main() {
    testFourWay();
    testDirectMapped();
    testFullyAssociative();
    testSingleLine();
    testInvalidConfigs();
    return testutil::finish("test_config");
}