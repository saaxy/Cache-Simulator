#pragma once

#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "Access.h"

namespace cachesim {

// Thrown for a malformed trace line. The message names the file and line.
class TraceParseError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

// Trace format, one access per line:
//     R 0x00001000
//     W 0x2000        # '#' starts a comment
// - Access type is R or W (either case). Fields are separated by spaces/tabs.
// - The address must be hexadecimal with a 0x prefix and fit in 64 bits
//   (leading zeros are fine).
// - Blank lines and comment-only lines are ignored; CRLF line endings and a
//   UTF-8 byte-order mark at the start of the file are tolerated.

// Parses a single line. Returns std::nullopt for blank/comment-only lines.
// Throws TraceParseError (message without location) if the line is malformed.
std::optional<TraceEntry> parseTraceLine(const std::string& line);

// Parses a whole stream. On a malformed line throws TraceParseError whose
// message starts with "<sourceName>:<line number>:".
std::vector<TraceEntry> parseTrace(std::istream& in, const std::string& sourceName);

// Opens and parses a file. Throws std::runtime_error if it cannot be opened.
std::vector<TraceEntry> parseTraceFile(const std::string& path);

}  // namespace cachesim
