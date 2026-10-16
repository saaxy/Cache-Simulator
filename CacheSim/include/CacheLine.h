#pragma once

#include <cstdint>

namespace cachesim {

// One cache line. The simulator tracks only metadata, never the data bytes.
struct CacheLine {
    bool valid = false;        // line holds a real block
    bool dirty = false;        // modified since fetch (used by write-back)
    std::uint64_t tag = 0;     // high address bits identifying the block in its set
    std::uint64_t stamp = 0;   // replacement metadata: a logical-clock value.
                               // Its meaning is set by the policy (Stage 5):
                               // LRU = time of last use, FIFO = time of insertion.
};

}  // namespace cachesim
