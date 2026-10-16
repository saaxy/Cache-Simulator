#pragma once

#include <ostream>
#include <string>

#include "CacheConfig.h"
#include "Simulation.h"

namespace cachesim {

// "direct-mapped" (1 way), "fully-associative" (one set, several ways), or
// "set-associative".
std::string organizationName(const CacheConfig& config);

// Human-readable report: one block per cache level plus the estimated AMAT.
// All numbers are results of a functional simulation, and the AMAT is a
// model-based estimate, not a hardware measurement.
void printTextReport(std::ostream& out, const RunResult& result, const std::string& traceName);

// Machine-readable output. csvHeader() and csvRow() always have the same number
// of columns (38). Notes on the columns:
//  - The first 25 columns describe L1 (or the only cache). Its memory_reads /
//    memory_writes are traffic to the NEXT level (L2 if present, else memory).
//  - l2_* columns are empty for a single-level run; l2_local_hit_rate is L2
//    hits / requests L2 received. For two-level runs the real memory traffic
//    is l2_memory_reads / l2_memory_writes.
//  - amat_cycles_estimate is a model-based estimate (see Simulation.h); it is
//    empty when the trace had no accesses.
// Rates are fractions in [0, 1] with 6 decimals and use '.' regardless of the
// system locale. With zero accesses the rates are 0.000000.
std::string csvHeader();
std::string csvRow(const RunResult& result, const std::string& traceName);

// Quotes a field if it contains a comma, quote, or newline (RFC 4180 style).
std::string csvEscape(const std::string& field);

}  // namespace cachesim
