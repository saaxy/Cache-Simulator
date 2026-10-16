#pragma once

#include <cstdint>

namespace cachesim {

enum class AccessType { Read, Write };
enum class AccessResult { Hit, Miss };

// One memory access from a trace.
struct TraceEntry {
    AccessType type;
    std::uint64_t address;
};

}  // namespace cachesim
