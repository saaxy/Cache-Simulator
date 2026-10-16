// Randomized differential test: the real Cache is compared, counter by counter,
// against a deliberately naive reference model written in a different style
// (per-set vectors kept in eviction order, no timestamps). A fixed seed makes
// every run identical. Reference semantics are the ones documented in Cache.h.
#include <random>
#include <vector>

#include "Cache.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

struct RefLine {
    std::uint64_t tag;
    bool dirty;
};

struct RefStats {
    std::uint64_t readHits = 0, readMisses = 0, writeHits = 0, writeMisses = 0;
    std::uint64_t evictions = 0, dirtyEvictions = 0, memoryReads = 0, memoryWrites = 0;
};

class ReferenceCache {
public:
    ReferenceCache(std::uint64_t capacity, std::uint64_t block, std::uint32_t ways,
                   ReplacementPolicy replacement, WritePolicy write, WriteAllocatePolicy alloc)
        : block_(block), ways_(ways), replacement_(replacement), write_(write), alloc_(alloc),
          sets_(capacity / block / ways) {}

    void access(bool isWrite, std::uint64_t address) {
        const std::uint64_t blockNumber = address / block_;
        std::vector<RefLine>& set = sets_[blockNumber % sets_.size()];
        const std::uint64_t tag = blockNumber / sets_.size();

        for (std::size_t i = 0; i < set.size(); ++i) {
            if (set[i].tag != tag) continue;
            RefLine line = set[i];
            if (replacement_ == ReplacementPolicy::LRU) {  // most recent goes to the back
                set.erase(set.begin() + static_cast<std::ptrdiff_t>(i));
                set.push_back(line);
            }
            RefLine& stored = (replacement_ == ReplacementPolicy::LRU) ? set.back() : set[i];
            if (isWrite) writeTo(stored);
            (isWrite ? stats.writeHits : stats.readHits)++;
            return;
        }

        (isWrite ? stats.writeMisses : stats.readMisses)++;
        if (isWrite && alloc_ == WriteAllocatePolicy::NoWriteAllocate) {
            stats.memoryWrites++;
            return;
        }
        if (set.size() == ways_) {  // front of the vector is the next victim
            if (set.front().dirty) {
                stats.dirtyEvictions++;
                stats.memoryWrites++;
            }
            stats.evictions++;
            set.erase(set.begin());
        }
        set.push_back({tag, false});
        stats.memoryReads++;
        if (isWrite) writeTo(set.back());
    }

    std::uint64_t dirtyLines() const {
        std::uint64_t n = 0;
        for (const auto& set : sets_)
            for (const RefLine& line : set) n += line.dirty ? 1 : 0;
        return n;
    }

    RefStats stats;

private:
    void writeTo(RefLine& line) {
        if (write_ == WritePolicy::WriteBack) line.dirty = true;
        else stats.memoryWrites++;
    }

    std::uint64_t block_;
    std::uint32_t ways_;
    ReplacementPolicy replacement_;
    WritePolicy write_;
    WriteAllocatePolicy alloc_;
    std::vector<std::vector<RefLine>> sets_;
};

void testRandomConfigurationsAgreeWithReference() {
    std::mt19937 rng(12345);
    const std::uint64_t capacities[] = {64, 128, 256, 512, 1024, 4096};
    const std::uint64_t blocks[] = {4, 8, 16, 32, 64};
    const std::uint32_t waysChoices[] = {1, 2, 4, 8, 0};  // 0 = fully associative

    int configurations = 0;
    for (int trial = 0; trial < 400; ++trial) {
        const std::uint64_t capacity = capacities[rng() % 6];
        const std::uint64_t block = blocks[rng() % 5];
        if (block > capacity) continue;
        const std::uint64_t lines = capacity / block;
        std::uint32_t ways = waysChoices[rng() % 5];
        if (ways == 0) ways = static_cast<std::uint32_t>(lines);
        if (ways > lines) continue;

        const ReplacementPolicy repl = (rng() % 2) ? ReplacementPolicy::LRU : ReplacementPolicy::FIFO;
        const WritePolicy write = (rng() % 2) ? WritePolicy::WriteBack : WritePolicy::WriteThrough;
        const WriteAllocatePolicy alloc =
            (rng() % 2) ? WriteAllocatePolicy::WriteAllocate : WriteAllocatePolicy::NoWriteAllocate;

        Cache cache(CacheConfig(capacity, block, ways, repl, write, alloc));
        ReferenceCache reference(capacity, block, ways, repl, write, alloc);

        // Mix of hot addresses, nearby addresses, and uniformly random ones.
        std::vector<std::uint64_t> hot;
        for (int i = 0, n = 2 + static_cast<int>(rng() % 10); i < n; ++i) hot.push_back(rng() % 16384);
        const int length = static_cast<int>(rng() % 300);
        for (int i = 0; i < length; ++i) {
            const unsigned kind = rng() % 10;
            std::uint64_t address = kind < 5 ? hot[rng() % hot.size()]
                                    : kind < 8 ? rng() % 16384
                                               : hot[rng() % hot.size()] + rng() % 64;
            const bool isWrite = (rng() % 3) == 0;
            cache.access(isWrite ? AccessType::Write : AccessType::Read, address);
            reference.access(isWrite, address);
        }

        const Statistics& s = cache.statistics();
        const RefStats& r = reference.stats;
        const bool same = s.readHits == r.readHits && s.readMisses == r.readMisses &&
                          s.writeHits == r.writeHits && s.writeMisses == r.writeMisses &&
                          s.evictions == r.evictions && s.dirtyEvictions == r.dirtyEvictions &&
                          s.memoryReads == r.memoryReads && s.memoryWrites == r.memoryWrites &&
                          cache.dirtyLineCount() == reference.dirtyLines();
        if (!same) {
            std::cerr << "mismatch: capacity " << capacity << " block " << block << " ways "
                      << ways << " " << toString(repl) << " " << toString(write) << " "
                      << toString(alloc) << "\n";
        }
        CHECK_EQ(same, true);
        ++configurations;
    }
    // Guard against the loop silently skipping nearly everything.
    CHECK_EQ(configurations > 250, true);
}

}  // namespace

int main() {
    testRandomConfigurationsAgreeWithReference();
    return testutil::finish("test_reference");
}
