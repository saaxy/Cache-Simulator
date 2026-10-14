// Tests for address decomposition (CacheConfig::split), checked by hand.
#include "CacheConfig.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

CacheConfig make(std::uint64_t cap, std::uint64_t block, std::uint32_t ways) {
    return CacheConfig(cap, block, ways, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                       WriteAllocatePolicy::WriteAllocate);
}

// 1 KiB direct-mapped, 16 B blocks: 64 sets, offset bits = 4, index bits = 6,
// so the tag is address >> 10.
//
// 0x1234 = 4660 decimal:
//   offset       = 4660 mod 16        = 4
//   blockAddress = 4660 / 16          = 291 (0x123)
//   setIndex     = 291 mod 64         = 35 (0x23)
//   tag          = 291 / 64           = 4
// Check: 4*1024 + 35*16 + 4 = 4096 + 560 + 4 = 4660.
void testWorkedExample() {
    const AddressParts p = make(1024, 16, 1).split(0x1234);
    CHECK_EQ(p.offset, 4u);
    CHECK_EQ(p.blockAddress, 0x123u);
    CHECK_EQ(p.setIndex, 35u);
    CHECK_EQ(p.tag, 4u);
}

void testAddressZero() {
    const AddressParts p = make(1024, 16, 1).split(0);
    CHECK_EQ(p.offset, 0u);
    CHECK_EQ(p.setIndex, 0u);
    CHECK_EQ(p.tag, 0u);
}

// All-ones 64-bit address: offset = 15, index = 63, tag = the top 54 bits
// all set = 2^54 - 1. Exercises the full 64-bit width.
void testMaxAddress() {
    const AddressParts p = make(1024, 16, 1).split(~std::uint64_t{0});
    CHECK_EQ(p.offset, 15u);
    CHECK_EQ(p.setIndex, 63u);
    CHECK_EQ(p.tag, (std::uint64_t{1} << 54) - 1);
}

// Same bytes, 64-way fully associative (one set): no index bits, so
// index = 0 and tag = blockAddress = 0x1234 >> 4 = 0x123.
void testFullyAssociativeHasNoIndex() {
    const AddressParts p = make(1024, 16, 64).split(0x1234);
    CHECK_EQ(p.offset, 4u);
    CHECK_EQ(p.setIndex, 0u);
    CHECK_EQ(p.tag, 0x123u);
}

// 4-way, 1 KiB, 16 B blocks: 64 lines / 4 = 16 sets, index bits = 4,
// tag = address >> 8. 0x1234: blockAddress 0x123, index = 0x123 mod 16 = 3,
// tag = 0x123 >> 4 = 0x12.
void testSetAssociative() {
    const AddressParts p = make(1024, 16, 4).split(0x1234);
    CHECK_EQ(p.offset, 4u);
    CHECK_EQ(p.setIndex, 3u);
    CHECK_EQ(p.tag, 0x12u);
}

// Addresses within one block share blockAddress, index, and tag.
void testSameBlockSameFields() {
    const CacheConfig c = make(1024, 16, 1);
    const AddressParts a = c.split(0x2000);
    const AddressParts b = c.split(0x200F);
    CHECK_EQ(a.blockAddress, b.blockAddress);
    CHECK_EQ(a.setIndex, b.setIndex);
    CHECK_EQ(a.tag, b.tag);
}

}  // namespace

int main() {
    testWorkedExample();
    testAddressZero();
    testMaxAddress();
    testFullyAssociativeHasNoIndex();
    testSetAssociative();
    testSameBlockSameFields();
    return testutil::finish("test_address");
}