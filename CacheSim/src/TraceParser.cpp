#include "TraceParser.h"

#include <cstdint>
#include <fstream>

namespace cachesim {

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::vector<std::string> splitFields(const std::string& text) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : text) {
        if (isSpace(c)) {
            if (!current.empty()) {
                fields.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        fields.push_back(current);
    }
    return fields;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Hand-rolled rather than strtoull: strtoull accepts signs, leading spaces and
// silently saturates on overflow, none of which we want in a validator.
std::uint64_t parseHexAddress(const std::string& token) {
    if (token.size() < 2 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) {
        throw TraceParseError("address must be hexadecimal with a 0x prefix, got '" + token +
                              "'");
    }
    if (token.size() == 2) {
        throw TraceParseError("address '" + token + "' has no hex digits after 0x");
    }
    std::uint64_t value = 0;
    for (std::size_t i = 2; i < token.size(); ++i) {
        const int digit = hexValue(token[i]);
        if (digit < 0) {
            throw TraceParseError("invalid hex digit '" + std::string(1, token[i]) +
                                  "' in address '" + token + "'");
        }
        if ((value >> 60) != 0) {  // another nibble would overflow 64 bits
            throw TraceParseError("address '" + token + "' does not fit in 64 bits");
        }
        value = (value << 4) | static_cast<std::uint64_t>(digit);
    }
    return value;
}

}  // namespace

std::optional<TraceEntry> parseTraceLine(const std::string& line) {
    const std::string code = line.substr(0, line.find('#'));
    const std::vector<std::string> fields = splitFields(code);
    if (fields.empty()) {
        return std::nullopt;
    }
    if (fields.size() != 2) {
        throw TraceParseError("expected '<R|W> <0xADDRESS>' but found " +
                              std::to_string(fields.size()) + " field(s)");
    }

    const std::string& op = fields[0];
    AccessType type;
    if (op == "R" || op == "r") {
        type = AccessType::Read;
    } else if (op == "W" || op == "w") {
        type = AccessType::Write;
    } else {
        throw TraceParseError("unknown access type '" + op + "' (expected R or W)");
    }
    return TraceEntry{type, parseHexAddress(fields[1])};
}

std::vector<TraceEntry> parseTrace(std::istream& in, const std::string& sourceName) {
    std::vector<TraceEntry> trace;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);  // UTF-8 BOM, which some Windows editors add
        }
        try {
            if (const std::optional<TraceEntry> entry = parseTraceLine(line)) {
                trace.push_back(*entry);
            }
        } catch (const TraceParseError& error) {
            throw TraceParseError(sourceName + ":" + std::to_string(lineNumber) + ": " +
                                  error.what());
        }
    }
    if (in.bad()) {
        throw std::runtime_error("I/O error while reading " + sourceName);
    }
    return trace;
}

std::vector<TraceEntry> parseTraceFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open trace file '" + path + "'");
    }
    return parseTrace(file, path);
}

}  // namespace cachesim
