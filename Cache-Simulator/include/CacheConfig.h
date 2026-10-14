#pragma once

#include <cstdint>

namespace cachesim {

enum class ReplacementPolicy { LRU, FIFO };
enum class WritePolicy { WriteThrough, WriteBack };
enum class WriteAllocatePolicy { WriteAllocate, NoWriteAllocate };

const char* toString(ReplacementPolicy policy);
const char* toString(WritePolicy policy);
const char* toString(WriteAllocatePolicy policy);

// An address split into the fields the cache uses.
//   address      = [ tag | set index | block offset ]
//   blockAddress = address without the offset = [ tag | set index ]
struct AddressParts {
    std::uint64_t tag = 0;
    std::uint64_t setIndex = 0;
    std::uint64_t offset = 0;
    std::uint64_t blockAddress = 0;
};

// Immutable, validated description of one cache. If construction succeeds,
// every derived value below is consistent. Throws std::invalid_argument otherwise.
class CacheConfig {
public:
    // ways = 1 gives a direct-mapped cache; ways = capacity / block size gives
    // a fully associative cache (see fullyAssociative()).
    CacheConfig(std::uint64_t capacityBytes,
                std::uint64_t blockSizeBytes,
                std::uint32_t ways,
                ReplacementPolicy replacement,
                WritePolicy writePolicy,
                WriteAllocatePolicy writeAllocate);

    // Convenience: one set containing every line.
    static CacheConfig fullyAssociative(std::uint64_t capacityBytes,
                                        std::uint64_t blockSizeBytes,
                                        ReplacementPolicy replacement,
                                        WritePolicy writePolicy,
                                        WriteAllocatePolicy writeAllocate);

    // Splits an address into tag, set index, and block offset for this geometry.
    AddressParts split(std::uint64_t address) const;

    std::uint64_t capacityBytes() const { return capacityBytes_; }
    std::uint64_t blockSizeBytes() const { return blockSizeBytes_; }
    std::uint32_t ways() const { return ways_; }
    std::uint64_t numLines() const { return numLines_; }
    std::uint64_t numSets() const { return numSets_; }
    std::uint32_t offsetBits() const { return offsetBits_; }  // log2(block size)
    std::uint32_t indexBits() const { return indexBits_; }    // log2(number of sets)

    ReplacementPolicy replacement() const { return replacement_; }
    WritePolicy writePolicy() const { return writePolicy_; }
    WriteAllocatePolicy writeAllocate() const { return writeAllocate_; }

private:
    std::uint64_t capacityBytes_;
    std::uint64_t blockSizeBytes_;
    std::uint32_t ways_;
    std::uint64_t numLines_;
    std::uint64_t numSets_;
    std::uint32_t offsetBits_;
    std::uint32_t indexBits_;
    ReplacementPolicy replacement_;
    WritePolicy writePolicy_;
    WriteAllocatePolicy writeAllocate_;
};

}  // namespace cachesim