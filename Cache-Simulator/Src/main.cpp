// Stage 2 placeholder: prints the derived geometry of a default configuration.
// Real command-line parsing arrives in Stage 7.
#include <exception>
#include <iostream>

#include "CacheConfig.h"

int main() {
    using namespace cachesim;
    try {
        const CacheConfig config(32 * 1024, 64, 4, ReplacementPolicy::LRU,
                                 WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate);
        std::cout << "Capacity:     " << config.capacityBytes() << " bytes\n"
                  << "Block size:   " << config.blockSizeBytes() << " bytes\n"
                  << "Ways:         " << config.ways() << "\n"
                  << "Lines:        " << config.numLines() << "\n"
                  << "Sets:         " << config.numSets() << "\n"
                  << "Offset bits:  " << config.offsetBits() << "\n"
                  << "Index bits:   " << config.indexBits() << "\n"
                  << "Replacement:  " << toString(config.replacement()) << "\n"
                  << "Write policy: " << toString(config.writePolicy()) << "\n"
                  << "Write miss:   " << toString(config.writeAllocate()) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}