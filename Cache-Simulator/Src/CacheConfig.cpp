#include "CacheConfig.h"

#include <stdexcept>
#include <string>

namespace cachesim {

namespace {

bool isPowerOfTwo(std::uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

// Only called on powers of two, so the result is exact.
std::uint32_t log2Exact(std::uint64_t value) {
    std::uint32_t bits = 0;
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

}  // namespace

const char* toString(ReplacementPolicy policy) {
    return policy == ReplacementPolicy::LRU ? "LRU" : "FIFO";
}

const char* toString(WritePolicy policy) {
    return policy == WritePolicy::WriteBack ? "write-back" : "write-through";
}

const char* toString(WriteAllocatePolicy policy) {
    return policy == WriteAllocatePolicy::WriteAllocate ? "write-allocate"
                                                        : "no-write-allocate";
}

CacheConfig::CacheConfig(std::uint64_t capacityBytes,
                         std::uint64_t blockSizeBytes,
                         std::uint32_t ways,
                         ReplacementPolicy replacement,
                         WritePolicy writePolicy,
                         WriteAllocatePolicy writeAllocate)
    : capacityBytes_(capacityBytes),
      blockSizeBytes_(blockSizeBytes),
      ways_(ways),
      numLines_(0),
      numSets_(0),
      offsetBits_(0),
      indexBits_(0),
      replacement_(replacement),
      writePolicy_(writePolicy),
      writeAllocate_(writeAllocate) {
    // Order matters: later checks divide by earlier-validated values.
    if (!isPowerOfTwo(capacityBytes)) {
        throw std::invalid_argument("cache capacity must be a power of two, got " +
                                    std::to_string(capacityBytes));
    }
    if (!isPowerOfTwo(blockSizeBytes)) {
        throw std::invalid_argument("block size must be a power of two, got " +
                                    std::to_string(blockSizeBytes));
    }
    if (blockSizeBytes > capacityBytes) {
        throw std::invalid_argument("block size (" + std::to_string(blockSizeBytes) +
                                    ") cannot exceed cache capacity (" +
                                    std::to_string(capacityBytes) + ")");
    }

    numLines_ = capacityBytes / blockSizeBytes;

    if (!isPowerOfTwo(ways)) {
        throw std::invalid_argument("associativity must be a power of two, got " +
                                    std::to_string(ways));
    }
    if (ways > numLines_) {
        throw std::invalid_argument("associativity (" + std::to_string(ways) +
                                    ") cannot exceed the number of lines (" +
                                    std::to_string(numLines_) + ")");
    }

    // Both numLines_ and ways are powers of two, so the division is exact
    // and numSets_ is also a power of two (required for bit-slicing the index).
    numSets_ = numLines_ / ways;
    offsetBits_ = log2Exact(blockSizeBytes);
    indexBits_ = log2Exact(numSets_);
}

AddressParts CacheConfig::split(std::uint64_t address) const {
    // Shift amounts are at most 63 (capacity is a 64-bit power of two), so no
    // shift ever reaches the full width, which would be undefined behavior.
    // numSets_ is a power of two, so (numSets_ - 1) is a mask of indexBits_ ones;
    // with a single set it is 0, giving index 0 without any shift.
    AddressParts parts;
    parts.offset = address & (blockSizeBytes_ - 1);
    parts.blockAddress = address >> offsetBits_;
    parts.setIndex = parts.blockAddress & (numSets_ - 1);
    parts.tag = parts.blockAddress >> indexBits_;
    return parts;
}

CacheConfig CacheConfig::fullyAssociative(std::uint64_t capacityBytes,
                                          std::uint64_t blockSizeBytes,
                                          ReplacementPolicy replacement,
                                          WritePolicy writePolicy,
                                          WriteAllocatePolicy writeAllocate) {
    // If sizes are invalid, pass 0 ways through; the constructor reports the
    // capacity/block error first, before it ever looks at ways.
    const std::uint64_t lines = blockSizeBytes == 0 ? 0 : capacityBytes / blockSizeBytes;
    return CacheConfig(capacityBytes, blockSizeBytes, static_cast<std::uint32_t>(lines),
                       replacement, writePolicy, writeAllocate);
}

}  // namespace cachesim