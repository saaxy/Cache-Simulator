// Reporting (text + CSV) for single-level and two-level runs, with a
// hand-calculated example.
#include <algorithm>
#include <sstream>
#include <string>

#include "Report.h"
#include "Simulation.h"
#include "TestUtil.h"

using namespace cachesim;

namespace {

CacheConfig directMapped256() {
    return CacheConfig(256, 16, 1, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                       WriteAllocatePolicy::WriteAllocate);
}

RunResult runSingle(const std::vector<TraceEntry>& trace) {
    return runSimulation(directMapped256(), std::nullopt, LatencyModel{}, trace);
}

std::size_t columnCount(const std::string& csvLine) {
    return static_cast<std::size_t>(std::count(csvLine.begin(), csvLine.end(), ',')) + 1;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// Direct-mapped 256 B, 16 B blocks (16 sets), write-back/allocate. Trace:
//   R 0x000 miss   R 0x000 hit   W 0x010 miss (set 1, dirty)
//   R 0x100 miss (set 0, evicts clean 0x000)
// accesses 4 (reads 3, writes 1), hits 1, misses 3, hit rate 0.25, miss 0.75,
// read hits 1, read misses 2, write hits 0, write misses 1, evictions 1,
// dirty evictions 0, memory reads 3, memory writes 0, dirty resident 1.
// Default latencies L1 4, memory 200: AMAT = 4 + 0.75 * 200 = 154.
const std::vector<TraceEntry> kTrace = {
    {AccessType::Read, 0x000},
    {AccessType::Read, 0x000},
    {AccessType::Write, 0x010},
    {AccessType::Read, 0x100},
};

void testSimulateMatchesHandCalculation() {
    const SimulationResult r = simulate(directMapped256(), kTrace);
    CHECK_EQ(r.stats.accesses(), 4u);
    CHECK_EQ(r.stats.hits(), 1u);
    CHECK_EQ(r.stats.misses(), 3u);
    CHECK_EQ(r.stats.evictions, 1u);
    CHECK_EQ(r.stats.memoryReads, 3u);
    CHECK_EQ(r.stats.memoryWrites, 0u);
    CHECK_EQ(r.dirtyLinesResident, 1u);
}

void testCsvRowMatchesHandCalculation() {
    const std::string expected =
        "t.trace,256,16,1,16,direct-mapped,LRU,write-back,write-allocate,"
        "4,3,1,1,3,0.250000,0.750000,1,2,0,1,1,0,3,0,1"
        ",,,,,,,,,"           // nine empty l2_* columns
        ",4,,200,154.000000";  // l1 latency, (empty l2 latency), memory latency, AMAT
    CHECK_EQ(csvRow(runSingle(kTrace), "t.trace"), expected);
}

void testCsvHeaderAndRowHaveSameColumnCount() {
    CHECK_EQ(columnCount(csvHeader()), columnCount(csvRow(runSingle(kTrace), "t.trace")));
    CHECK_EQ(columnCount(csvHeader()), 38u);
}

void testCsvEscape() {
    CHECK_EQ(csvEscape("plain.trace"), std::string("plain.trace"));
    CHECK_EQ(csvEscape("a,b"), std::string("\"a,b\""));
    CHECK_EQ(csvEscape("say \"hi\""), std::string("\"say \"\"hi\"\"\""));
    CHECK_EQ(csvEscape("two\nlines"), std::string("\"two\nlines\""));
}

// A trace name with a comma must not add a column once escaped.
void testCsvRowWithCommaInTraceName() {
    const std::string row = csvRow(runSingle(kTrace), "my,trace.txt");
    CHECK_EQ(row.rfind("\"my,trace.txt\",256,", 0), 0u);
}

// Zero accesses must produce a valid report and CSV without dividing by zero;
// AMAT is undefined and shown as n/a / an empty CSV field.
void testEmptyTrace() {
    const RunResult r = runSingle({});
    CHECK_EQ(r.l1.stats.accesses(), 0u);

    std::ostringstream text;
    printTextReport(text, r, "empty.trace");
    CHECK_EQ(contains(text.str(), "n/a"), true);
    CHECK_EQ(contains(text.str(), "Accesses:       0"), true);
    CHECK_EQ(contains(text.str(), "Estimated AMAT: n/a (no accesses)"), true);

    const std::string row = csvRow(r, "empty.trace");
    CHECK_EQ(contains(row, ",0.000000,0.000000,"), true);
    CHECK_EQ(columnCount(row), 38u);
    CHECK_EQ(row.back(), ',');  // empty final amat column
}

void testOrganizationNames() {
    CHECK_EQ(organizationName(directMapped256()), std::string("direct-mapped"));
    CHECK_EQ(organizationName(CacheConfig(256, 16, 4, ReplacementPolicy::LRU,
                                          WritePolicy::WriteBack,
                                          WriteAllocatePolicy::WriteAllocate)),
             std::string("set-associative"));
    CHECK_EQ(organizationName(CacheConfig::fullyAssociative(
                 256, 16, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                 WriteAllocatePolicy::WriteAllocate)),
             std::string("fully-associative"));
}

void testTextReportContents() {
    std::ostringstream out;
    printTextReport(out, runSingle(kTrace), "t.trace");
    const std::string text = out.str();
    CHECK_EQ(contains(text, "not a hardware measurement"), true);
    CHECK_EQ(contains(text, "direct-mapped (1-way, 16 sets)"), true);
    CHECK_EQ(contains(text, "write-back"), true);
    CHECK_EQ(contains(text, "25.00%"), true);  // overall hit rate 1/4
    CHECK_EQ(contains(text, "75.00%"), true);  // overall miss rate 3/4
    CHECK_EQ(contains(text, "33.33%"), true);  // reads: 1 of 3
    CHECK_EQ(contains(text, "0.00%"), true);   // writes: 0 of 1
    CHECK_EQ(contains(text, "model-based estimate"), true);
    CHECK_EQ(contains(text, "Estimated AMAT: 154.00 cycles"), true);
}

// Two-level report: both blocks appear, and the CSV carries the L2 columns.
// L1 64 B direct-mapped, 16 B blocks; L2 256 B 4-way, 16 B blocks.
// R 0x00, R 0x40, R 0x00, R 0x40: L1 misses all 4 (0x00 and 0x40 share L1
// set 0); L2 sees 4 reads: miss, miss, hit, hit.
// AMAT = 4 + 1.0 * (12 + 0.5 * 200) = 116.
void testTwoLevelReport() {
    const CacheConfig l1(64, 16, 1, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                         WriteAllocatePolicy::WriteAllocate);
    const CacheConfig l2(256, 16, 4, ReplacementPolicy::LRU, WritePolicy::WriteBack,
                         WriteAllocatePolicy::WriteAllocate);
    const std::vector<TraceEntry> trace = {{AccessType::Read, 0x00}, {AccessType::Read, 0x40},
                                           {AccessType::Read, 0x00}, {AccessType::Read, 0x40}};
    const RunResult r = runSimulation(l1, l2, LatencyModel{}, trace);

    std::ostringstream out;
    printTextReport(out, r, "t.trace");
    CHECK_EQ(contains(out.str(), "L1 cache"), true);
    CHECK_EQ(contains(out.str(), "L2 cache"), true);
    CHECK_EQ(contains(out.str(), "Estimated AMAT: 116.00 cycles"), true);

    const std::string row = csvRow(r, "t.trace");
    CHECK_EQ(columnCount(row), 38u);
    // L2 columns: 256 B, 16 B blocks, 4 ways, 4 accesses, 2 hits, 2 misses,
    // local hit rate 0.5, 2 memory reads, 0 memory writes; then latencies and AMAT.
    CHECK_EQ(contains(row, ",256,16,4,4,2,2,0.500000,2,0,4,12,200,116.000000"), true);
}

}  // namespace

int main() {
    testSimulateMatchesHandCalculation();
    testCsvRowMatchesHandCalculation();
    testCsvHeaderAndRowHaveSameColumnCount();
    testCsvEscape();
    testCsvRowWithCommaInTraceName();
    testEmptyTrace();
    testOrganizationNames();
    testTextReportContents();
    testTwoLevelReport();
    return testutil::finish("test_report");
}
