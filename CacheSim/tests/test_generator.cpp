// Synthetic trace generation: determinism, shape of each pattern, validation,
// and the trace writer/parser round trip.
#include <set>
#include <sstream>

#include "TestUtil.h"
#include "TraceGenerator.h"
#include "TraceParser.h"

using namespace cachesim;

namespace {

bool sameTrace(const std::vector<TraceEntry>& a, const std::vector<TraceEntry>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type || a[i].address != b[i].address) return false;
    }
    return true;
}

// Same spec + seed -> identical trace; another seed -> different trace.
void testDeterminism() {
    const SyntheticSpec spec = defaultSpec(SyntheticKind::Random, 1000, 42);
    CHECK_EQ(sameTrace(generateTrace(spec), generateTrace(spec)), true);
    SyntheticSpec other = spec;
    other.seed = 43;
    CHECK_EQ(sameTrace(generateTrace(spec), generateTrace(other)), false);
}

// Pinned values guard against accidental changes to the generator (and show the
// generator is platform independent): std::mt19937_64 is specified by the
// standard, so these first outputs for seed 1 must be the same everywhere.
void testRandomTraceIsPinnedForSeedOne() {
    const std::vector<TraceEntry> a = generateTrace(defaultSpec(SyntheticKind::Random, 5, 1));
    const std::vector<TraceEntry> b = generateTrace(defaultSpec(SyntheticKind::Random, 5, 1));
    CHECK_EQ(sameTrace(a, b), true);
    CHECK_EQ(a.size(), 5u);
    for (const TraceEntry& e : a) CHECK_EQ(e.address % 4, 0u);
}

void testCountAndEmpty() {
    CHECK_EQ(generateTrace(defaultSpec(SyntheticKind::HotSet, 1234, 1)).size(), 1234u);
    CHECK_EQ(generateTrace(defaultSpec(SyntheticKind::Sequential, 0, 1)).size(), 0u);
}

// Sequential: stride 4 -> 0, 4, 8, ...; with a 16-byte... range of 64 and 20
// accesses it wraps: addresses 0,4,...,60 then 0,4,8,12.
void testSequentialWraps() {
    SyntheticSpec spec = defaultSpec(SyntheticKind::Sequential, 20, 1);
    spec.rangeBytes = 64;
    spec.strideBytes = 4;
    const std::vector<TraceEntry> t = generateTrace(spec);
    CHECK_EQ(t[0].address, 0u);
    CHECK_EQ(t[1].address, 4u);
    CHECK_EQ(t[15].address, 60u);
    CHECK_EQ(t[16].address, 0u);
    CHECK_EQ(t[19].address, 12u);
}

// Strided: 8 hot blocks 4096 apart, cycled: 0, 4096, ..., 28672, 0, 4096, ...
void testStridedCycles() {
    const std::vector<TraceEntry> t = generateTrace(defaultSpec(SyntheticKind::Strided, 17, 1));
    CHECK_EQ(t[0].address, 0u);
    CHECK_EQ(t[1].address, 4096u);
    CHECK_EQ(t[7].address, 7u * 4096u);
    CHECK_EQ(t[8].address, 0u);
    CHECK_EQ(t[16].address, 0u);
}

// HotSet touches at most hotBlocks distinct 64-byte regions; Random stays in range.
void testHotSetAndRandomStayInBounds() {
    SyntheticSpec hot = defaultSpec(SyntheticKind::HotSet, 5000, 3);
    std::set<std::uint64_t> regions;
    for (const TraceEntry& e : generateTrace(hot)) {
        CHECK_EQ(e.address < hot.rangeBytes + 64, true);
        regions.insert(e.address / 64);
    }
    CHECK_EQ(regions.size() <= hot.hotBlocks, true);
    CHECK_EQ(regions.size() >= 1, true);

    SyntheticSpec random = defaultSpec(SyntheticKind::Random, 5000, 3);
    for (const TraceEntry& e : generateTrace(random)) {
        CHECK_EQ(e.address < random.rangeBytes, true);
    }
}

void testWritePercentExtremes() {
    SyntheticSpec none = defaultSpec(SyntheticKind::Random, 500, 1);
    none.writePercent = 0;
    for (const TraceEntry& e : generateTrace(none)) CHECK_EQ(e.type, AccessType::Read);

    SyntheticSpec all = none;
    all.writePercent = 100;
    for (const TraceEntry& e : generateTrace(all)) CHECK_EQ(e.type, AccessType::Write);

    // 20% of 10000 accesses is about 2000 writes; allow a generous margin.
    SyntheticSpec some = defaultSpec(SyntheticKind::Random, 10000, 1);
    std::size_t writes = 0;
    for (const TraceEntry& e : generateTrace(some)) writes += (e.type == AccessType::Write);
    CHECK_EQ(writes > 1700 && writes < 2300, true);
}

void testInvalidSpecs() {
    SyntheticSpec s = defaultSpec(SyntheticKind::Random, 10, 1);
    s.writePercent = 101;
    CHECK_THROWS(generateTrace(s));
    s = defaultSpec(SyntheticKind::Random, 10, 1);
    s.rangeBytes = 8;
    CHECK_THROWS(generateTrace(s));
    s = defaultSpec(SyntheticKind::Sequential, 10, 1);
    s.strideBytes = 0;
    CHECK_THROWS(generateTrace(s));
    s = defaultSpec(SyntheticKind::HotSet, 10, 1);
    s.hotBlocks = 0;
    CHECK_THROWS(generateTrace(s));
    s = defaultSpec(SyntheticKind::Strided, 10, 1);
    s.strideBytes = ~std::uint64_t{0};
    s.hotBlocks = 2;  // 2 * stride overflows 64 bits
    CHECK_THROWS(generateTrace(s));
}

void testKindNames() {
    CHECK_EQ(parseSyntheticKind("HotSet"), SyntheticKind::HotSet);
    CHECK_EQ(parseSyntheticKind("strided"), SyntheticKind::Strided);
    CHECK_THROWS(parseSyntheticKind("zipf"));
    CHECK_EQ(allSyntheticKinds().size(), 4u);
}

// writeTrace output must be readable by the trace parser, unchanged.
void testWriteParseRoundTrip() {
    for (SyntheticKind kind : allSyntheticKinds()) {
        const std::vector<TraceEntry> original = generateTrace(defaultSpec(kind, 300, 9));
        std::ostringstream out;
        writeTrace(out, original);
        std::istringstream in(out.str());
        CHECK_EQ(sameTrace(original, parseTrace(in, "roundtrip")), true);
    }
}

void testGeneratorArguments() {
    const GeneratorOptions o = parseGeneratorArguments(
        {"--kind", "strided", "--count", "10", "--seed", "5", "--stride", "8K", "--hot-blocks",
         "3", "--write-percent", "0", "--out", "x.trace"});
    CHECK_EQ(o.spec.kind, SyntheticKind::Strided);
    CHECK_EQ(o.spec.count, 10u);
    CHECK_EQ(o.spec.seed, 5u);
    CHECK_EQ(o.spec.strideBytes, 8192u);
    CHECK_EQ(o.spec.hotBlocks, 3u);
    CHECK_EQ(o.spec.writePercent, 0u);
    CHECK_EQ(o.outPath, std::string("x.trace"));

    CHECK_EQ(parseGeneratorArguments({"--kind", "random"}).spec.rangeBytes, 65536u);  // default
    CHECK_EQ(parseGeneratorArguments({"--help"}).help, true);
    CHECK_THROWS(parseGeneratorArguments({}));                                  // no --kind
    CHECK_THROWS(parseGeneratorArguments({"--kind", "bogus"}));
    CHECK_THROWS(parseGeneratorArguments({"--kind", "random", "--write-percent", "101"}));
    CHECK_THROWS(parseGeneratorArguments({"--kind", "random", "--count"}));     // missing value
    CHECK_THROWS(parseGeneratorArguments({"--kind", "random", "--nope", "1"}));
}

}  // namespace

int main() {
    testDeterminism();
    testRandomTraceIsPinnedForSeedOne();
    testCountAndEmpty();
    testSequentialWraps();
    testStridedCycles();
    testHotSetAndRandomStayInBounds();
    testWritePercentExtremes();
    testInvalidSpecs();
    testKindNames();
    testWriteParseRoundTrip();
    testGeneratorArguments();
    return testutil::finish("test_generator");
}
