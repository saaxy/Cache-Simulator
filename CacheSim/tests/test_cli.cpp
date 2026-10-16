// Command-line option parsing and configuration building.
#include <string>
#include <vector>

#include "CommandLine.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

using Args = std::vector<std::string>;

void testDefaults() {
    const Options o = parseArguments({"--trace", "t.trace"});
    CHECK_EQ(o.tracePath, std::string("t.trace"));
    CHECK_EQ(o.capacityBytes, 32768u);
    CHECK_EQ(o.blockSizeBytes, 64u);
    CHECK_EQ(o.ways, 4u);
    CHECK_EQ(o.fullyAssociative, false);
    CHECK_EQ(o.replacement, ReplacementPolicy::LRU);
    CHECK_EQ(o.writePolicy, WritePolicy::WriteBack);
    CHECK_EQ(o.writeAllocate, WriteAllocatePolicy::WriteAllocate);
    CHECK_EQ(o.csv, false);
    CHECK_EQ(o.csvHeader, true);

    // 32 KiB / 64 B = 512 lines; 4 ways -> 128 sets.
    const CacheConfig c = makeCacheConfig(o);
    CHECK_EQ(c.numSets(), 128u);
}

void testAllOptions() {
    const Options o = parseArguments(
        {"--trace", "x.txt", "--size", "8K", "--block", "32", "--assoc", "2", "--replacement",
         "fifo", "--write-policy", "wt", "--write-miss", "no-allocate", "--csv", "--no-header"});
    CHECK_EQ(o.capacityBytes, 8192u);
    CHECK_EQ(o.blockSizeBytes, 32u);
    CHECK_EQ(o.ways, 2u);
    CHECK_EQ(o.replacement, ReplacementPolicy::FIFO);
    CHECK_EQ(o.writePolicy, WritePolicy::WriteThrough);
    CHECK_EQ(o.writeAllocate, WriteAllocatePolicy::NoWriteAllocate);
    CHECK_EQ(o.csv, true);
    CHECK_EQ(o.csvHeader, false);
}

void testEqualsFormAndSizeSuffixes() {
    CHECK_EQ(parseArguments({"--trace=t", "--size=1M"}).capacityBytes, 1048576u);
    CHECK_EQ(parseArguments({"--trace", "t", "--size", "4KB"}).capacityBytes, 4096u);
    CHECK_EQ(parseArguments({"--trace", "t", "--size", "4k"}).capacityBytes, 4096u);
    CHECK_EQ(parseArguments({"--trace", "t", "--size", "1g"}).capacityBytes, 1073741824u);
    CHECK_EQ(parseArguments({"--trace", "t", "--size", "2048"}).capacityBytes, 2048u);
}

// 32 KiB / 64 B = 512 lines, all in one set.
void testFullyAssociativeOption() {
    const Options o = parseArguments({"--trace", "t", "--assoc", "full"});
    CHECK_EQ(o.fullyAssociative, true);
    const CacheConfig c = makeCacheConfig(o);
    CHECK_EQ(c.ways(), 512u);
    CHECK_EQ(c.numSets(), 1u);
}

void testHelpNeedsNoTrace() {
    CHECK_EQ(parseArguments({"--help"}).help, true);
    CHECK_EQ(parseArguments({"-h"}).help, true);
}

void testUsageErrors() {
    CHECK_THROWS(parseArguments({}));                                   // no --trace
    CHECK_THROWS(parseArguments({"--size", "4K"}));                     // no --trace
    CHECK_THROWS(parseArguments({"--trace", "t", "--bogus"}));          // unknown option
    CHECK_THROWS(parseArguments({"--trace"}));                          // missing value
    CHECK_THROWS(parseArguments({"--trace", "t", "--size"}));           // missing value
    CHECK_THROWS(parseArguments({"--trace", "t", "--size", "abc"}));    // not a number
    CHECK_THROWS(parseArguments({"--trace", "t", "--size", "-5"}));     // negative
    CHECK_THROWS(parseArguments({"--trace", "t", "--size", "12x"}));    // bad suffix
    CHECK_THROWS(parseArguments({"--trace", "t", "--size", "99999999999999999999"}));  // overflow
    CHECK_THROWS(parseArguments({"--trace", "t", "--size", "99999999999G"}));          // overflow
    CHECK_THROWS(parseArguments({"--trace", "t", "--assoc", "4294967296"}));           // > 32 bits
    CHECK_THROWS(parseArguments({"--trace", "t", "--replacement", "random"}));
    CHECK_THROWS(parseArguments({"--trace", "t", "--write-policy", "x"}));
    CHECK_THROWS(parseArguments({"--trace", "t", "--write-miss", "x"}));
    CHECK_THROWS(parseArguments({"--trace", "t", "--csv=1"}));          // flag with a value
}

// Option syntax is fine, but the resulting cache geometry is invalid; the
// error comes from CacheConfig validation.
void testInvalidGeometryIsRejectedWhenBuildingConfig() {
    CHECK_THROWS(makeCacheConfig(parseArguments({"--trace", "t", "--size", "1000"})));
    CHECK_THROWS(makeCacheConfig(parseArguments({"--trace", "t", "--block", "48"})));
    CHECK_THROWS(makeCacheConfig(parseArguments({"--trace", "t", "--assoc", "0"})));
    CHECK_THROWS(makeCacheConfig(parseArguments({"--trace", "t", "--assoc", "3"})));
    CHECK_THROWS(makeCacheConfig(parseArguments({"--trace", "t", "--block", "64K"})));  // block > cache
}


void testL2AndLatencyOptions() {
    const Options none = parseArguments({"--trace", "t"});
    CHECK_EQ(none.hasL2, false);
    CHECK_EQ(makeL2Config(none).has_value(), false);
    CHECK_EQ(none.latency.l1HitCycles, 4.0);
    CHECK_EQ(none.latency.l2HitCycles, 12.0);
    CHECK_EQ(none.latency.memoryCycles, 200.0);

    const Options o = parseArguments({"--trace", "t", "--l2-size", "256K", "--l2-assoc", "16",
                                      "--l2-block", "128", "--l2-replacement", "fifo",
                                      "--l1-latency", "3", "--l2-latency", "14",
                                      "--mem-latency", "250"});
    CHECK_EQ(o.hasL2, true);
    const std::optional<CacheConfig> l2 = makeL2Config(o);
    CHECK_EQ(l2.has_value(), true);
    CHECK_EQ(l2->capacityBytes(), 262144u);
    CHECK_EQ(l2->blockSizeBytes(), 128u);
    CHECK_EQ(l2->ways(), 16u);
    CHECK_EQ(l2->replacement(), ReplacementPolicy::FIFO);
    CHECK_EQ(l2->writePolicy(), WritePolicy::WriteBack);               // fixed for L2
    CHECK_EQ(l2->writeAllocate(), WriteAllocatePolicy::WriteAllocate);  // fixed for L2
    CHECK_EQ(o.latency.l1HitCycles, 3.0);
    CHECK_EQ(o.latency.l2HitCycles, 14.0);
    CHECK_EQ(o.latency.memoryCycles, 250.0);

    // L2 block size defaults to the L1 block size; --l2-assoc full works.
    const Options d = parseArguments({"--trace", "t", "--block", "32", "--l2-size", "4K",
                                      "--l2-assoc", "full"});
    CHECK_EQ(makeL2Config(d)->blockSizeBytes(), 32u);
    CHECK_EQ(makeL2Config(d)->numSets(), 1u);
}

void testL2ErrorsAndLatencyErrors() {
    CHECK_THROWS(parseArguments({"--trace", "t", "--l2-assoc", "8"}));        // needs --l2-size
    CHECK_THROWS(parseArguments({"--trace", "t", "--l2-block", "64"}));       // needs --l2-size
    CHECK_THROWS(parseArguments({"--trace", "t", "--l2-replacement", "lru"}));
    CHECK_THROWS(parseArguments({"--trace", "t", "--mem-latency", "-5"}));
    CHECK_THROWS(parseArguments({"--trace", "t", "--l1-latency", "fast"}));
    CHECK_THROWS(makeL2Config(parseArguments({"--trace", "t", "--l2-size", "1000"})));
}

}  // namespace

int main() {
    testDefaults();
    testAllOptions();
    testEqualsFormAndSizeSuffixes();
    testFullyAssociativeOption();
    testHelpNeedsNoTrace();
    testUsageErrors();
    testInvalidGeometryIsRejectedWhenBuildingConfig();
    testL2AndLatencyOptions();
    testL2ErrorsAndLatencyErrors();
    return testutil::finish("test_cli");
}
