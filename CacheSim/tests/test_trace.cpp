// Trace parsing: valid formats, malformed lines, empty traces, error locations.
#include <sstream>
#include <string>

#include "TestUtil.h"
#include "TraceParser.h"

using namespace cachesim;

namespace {

template <typename F>
std::string messageOf(F&& action) {
    try {
        action();
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::vector<TraceEntry> parseString(const std::string& text) {
    std::istringstream in(text);
    return parseTrace(in, "t.trace");
}

void testValidLines() {
    const auto read = parseTraceLine("R 0x00001000");
    CHECK_EQ(read.has_value(), true);
    CHECK_EQ(read->type, AccessType::Read);
    CHECK_EQ(read->address, 0x1000u);

    const auto write = parseTraceLine("W 0xdeadBEEF");  // mixed-case hex digits
    CHECK_EQ(write->type, AccessType::Write);
    CHECK_EQ(write->address, 0xDEADBEEFu);

    CHECK_EQ(parseTraceLine("r 0X10")->type, AccessType::Read);   // lowercase op, 0X prefix
    CHECK_EQ(parseTraceLine("w 0x10")->type, AccessType::Write);
    CHECK_EQ(parseTraceLine("  R \t 0x10  ")->address, 0x10u);    // extra whitespace
    CHECK_EQ(parseTraceLine("R 0x10\r")->address, 0x10u);         // CRLF leftover
    CHECK_EQ(parseTraceLine("R 0x10 # inline comment")->address, 0x10u);
}

void testBlankAndCommentLinesAreSkipped() {
    CHECK_EQ(parseTraceLine("").has_value(), false);
    CHECK_EQ(parseTraceLine("   \t ").has_value(), false);
    CHECK_EQ(parseTraceLine("# just a comment").has_value(), false);
    CHECK_EQ(parseTraceLine("   # indented comment").has_value(), false);
}

// Full 64-bit range is accepted; leading zeros do not count against the width.
void testAddressRange() {
    CHECK_EQ(parseTraceLine("R 0xFFFFFFFFFFFFFFFF")->address, ~std::uint64_t{0});
    CHECK_EQ(parseTraceLine("R 0x00000000000000000001")->address, 1u);  // 20 digits
    CHECK_EQ(parseTraceLine("R 0x0")->address, 0u);
}

void testMalformedLines() {
    CHECK_THROWS_AS(parseTraceLine("X 0x10"), TraceParseError);       // unknown op
    CHECK_THROWS_AS(parseTraceLine("RW 0x10"), TraceParseError);      // op too long
    CHECK_THROWS_AS(parseTraceLine("R 1000"), TraceParseError);       // no 0x prefix
    CHECK_THROWS_AS(parseTraceLine("R 0x"), TraceParseError);         // no digits
    CHECK_THROWS_AS(parseTraceLine("R 0xZZ"), TraceParseError);       // bad digit
    CHECK_THROWS_AS(parseTraceLine("R 0x1_0"), TraceParseError);      // bad digit
    CHECK_THROWS_AS(parseTraceLine("R -0x10"), TraceParseError);      // sign
    CHECK_THROWS_AS(parseTraceLine("R 0x10 junk"), TraceParseError);  // extra field
    CHECK_THROWS_AS(parseTraceLine("R"), TraceParseError);            // missing address
    CHECK_THROWS_AS(parseTraceLine("0x10"), TraceParseError);         // missing op
    CHECK_THROWS_AS(parseTraceLine("R 0x10000000000000000"), TraceParseError);  // 2^64
}

void testEmptyAndCommentOnlyTraces() {
    CHECK_EQ(parseString("").size(), 0u);
    CHECK_EQ(parseString("\n\n").size(), 0u);
    CHECK_EQ(parseString("# nothing here\n   \n").size(), 0u);
}

void testMultiLineTrace() {
    const auto trace = parseString("R 0x1000\r\n\n# c\nW 0x2000\nR 0x1004");  // no final newline
    CHECK_EQ(trace.size(), 3u);
    CHECK_EQ(trace[0].address, 0x1000u);
    CHECK_EQ(trace[1].type, AccessType::Write);
    CHECK_EQ(trace[2].address, 0x1004u);
}

void testUtf8ByteOrderMarkIsIgnored() {
    const auto trace = parseString("\xEF\xBB\xBF" "R 0x10\nW 0x20\n");
    CHECK_EQ(trace.size(), 2u);
    CHECK_EQ(trace[0].address, 0x10u);
}

// The error message must point at the offending line (line 4 here, counting
// the blank line) and name the source.
void testErrorMessagesNameFileAndLine() {
    const std::string message = messageOf([] { parseString("R 0x10\n\nW 0x20\nbad line here\n"); });
    CHECK_EQ(contains(message, "t.trace:4:"), true);
    CHECK_EQ(contains(message, "field"), true);

    const std::string badHex = messageOf([] { parseString("R 0x10\nW 0xGG\n"); });
    CHECK_EQ(contains(badHex, "t.trace:2:"), true);
    CHECK_EQ(contains(badHex, "invalid hex digit"), true);
}

void testMissingFileIsRuntimeError() {
    CHECK_THROWS_AS(parseTraceFile("this/file/does/not/exist.trace"), std::runtime_error);
    // A missing file is an I/O problem, not a malformed trace.
    bool wasParseError = false;
    try {
        parseTraceFile("this/file/does/not/exist.trace");
    } catch (const TraceParseError&) {
        wasParseError = true;
    } catch (...) {
    }
    CHECK_EQ(wasParseError, false);
}

}  // namespace

int main() {
    testValidLines();
    testBlankAndCommentLinesAreSkipped();
    testAddressRange();
    testMalformedLines();
    testEmptyAndCommentOnlyTraces();
    testMultiLineTrace();
    testUtf8ByteOrderMarkIsIgnored();
    testErrorMessagesNameFileAndLine();
    testMissingFileIsRuntimeError();
    return testutil::finish("test_trace");
}
