#include "Report.h"

#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace cachesim {

namespace {

std::string formatFixed(double value, int decimals) {
    std::ostringstream out;
    out.imbue(std::locale::classic());  // always '.' as the decimal separator
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

// Plain number without trailing zeros (4 -> "4", 4.5 -> "4.5"), locale-independent.
std::string formatNumber(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(15) << value;
    return out.str();
}

std::string percentText(std::uint64_t part, std::uint64_t total) {
    if (total == 0) {
        return "n/a";
    }
    return formatFixed(100.0 * static_cast<double>(part) / static_cast<double>(total), 2) +
           "%";
}

std::string associativityText(const CacheConfig& config) {
    if (config.ways() == 1) {
        return "direct-mapped (1-way, " + std::to_string(config.numSets()) + " sets)";
    }
    if (config.numSets() == 1) {
        return "fully associative (" + std::to_string(config.ways()) + " ways, 1 set)";
    }
    return std::to_string(config.ways()) + "-way set-associative (" +
           std::to_string(config.numSets()) + " sets)";
}

std::string joinCsv(const std::vector<std::string>& fields) {
    std::string line;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) line += ',';
        line += fields[i];
    }
    return line;
}

void printLevel(std::ostream& out, const std::string& title, const SimulationResult& level,
                const std::string& nextLevel) {
    const CacheConfig& config = level.config;
    const Statistics& s = level.stats;
    out << title << "\n"
        << "  Capacity:       " << config.capacityBytes() << " bytes\n"
        << "  Block size:     " << config.blockSizeBytes() << " bytes\n"
        << "  Associativity:  " << associativityText(config) << "\n"
        << "  Replacement:    " << toString(config.replacement()) << "\n"
        << "  Write policy:   " << toString(config.writePolicy()) << "\n"
        << "  Write miss:     " << toString(config.writeAllocate()) << "\n"
        << "  Accesses:       " << s.accesses() << "\n"
        << "    Reads:        " << s.reads() << "  (hits " << s.readHits << ", misses "
        << s.readMisses << ", hit rate " << percentText(s.readHits, s.reads()) << ")\n"
        << "    Writes:       " << s.writes() << "  (hits " << s.writeHits << ", misses "
        << s.writeMisses << ", hit rate " << percentText(s.writeHits, s.writes()) << ")\n"
        << "  Hits:           " << s.hits() << "  (" << percentText(s.hits(), s.accesses())
        << ")\n"
        << "  Misses:         " << s.misses() << "  (" << percentText(s.misses(), s.accesses())
        << ")\n"
        << "  Evictions:      " << s.evictions << "  (dirty " << s.dirtyEvictions << ")\n"
        << "  Traffic to " << nextLevel << " (transactions, not bytes): " << s.memoryReads
        << " block fetches, " << s.memoryWrites << " writes\n"
        << "  Dirty lines still resident at the end (not flushed): "
        << level.dirtyLinesResident << "\n";
}

}  // namespace

std::string organizationName(const CacheConfig& config) {
    if (config.ways() == 1) return "direct-mapped";
    if (config.numSets() == 1) return "fully-associative";
    return "set-associative";
}

void printTextReport(std::ostream& out, const RunResult& result, const std::string& traceName) {
    out << "CacheSim result (functional simulation; not a hardware measurement)\n"
        << "Trace: " << traceName << "\n\n";

    if (result.l2) {
        printLevel(out, "L1 cache", result.l1, "L2");
        out << "\n";
        printLevel(out, "L2 cache (accesses = requests received from L1)", *result.l2,
                   "memory");
    } else {
        printLevel(out, "Cache", result.l1, "memory");
    }

    out << "\nEstimated AMAT (model-based estimate in cycles; not a hardware measurement)\n";
    if (result.l2) {
        out << "  Model: L1 hit + L1 miss rate x (L2 hit + L2 local miss rate x memory)\n"
            << "  Assumed latencies: L1 hit " << formatNumber(result.latency.l1HitCycles)
            << ", L2 hit " << formatNumber(result.latency.l2HitCycles) << ", memory "
            << formatNumber(result.latency.memoryCycles) << " cycles\n";
    } else {
        out << "  Model: L1 hit + L1 miss rate x memory\n"
            << "  Assumed latencies: L1 hit " << formatNumber(result.latency.l1HitCycles)
            << ", memory " << formatNumber(result.latency.memoryCycles) << " cycles\n";
    }
    const std::optional<double> amat = estimatedAmat(result);
    if (amat) {
        out << "  Estimated AMAT: " << formatFixed(*amat, 2) << " cycles\n";
    } else {
        out << "  Estimated AMAT: n/a (no accesses)\n";
    }
}

std::string csvHeader() {
    return "trace,capacity_bytes,block_size_bytes,ways,num_sets,organization,replacement,"
           "write_policy,write_miss_policy,accesses,reads,writes,hits,misses,hit_rate,"
           "miss_rate,read_hits,read_misses,write_hits,write_misses,evictions,"
           "dirty_evictions,memory_reads,memory_writes,dirty_lines_resident,"
           "l2_capacity_bytes,l2_block_size_bytes,l2_ways,l2_accesses,l2_hits,l2_misses,"
           "l2_local_hit_rate,l2_memory_reads,l2_memory_writes,"
           "l1_hit_cycles,l2_hit_cycles,memory_cycles,amat_cycles_estimate";
}

std::string csvRow(const RunResult& result, const std::string& traceName) {
    const CacheConfig& c = result.l1.config;
    const Statistics& s = result.l1.stats;
    std::vector<std::string> fields = {
        csvEscape(traceName),
        std::to_string(c.capacityBytes()),
        std::to_string(c.blockSizeBytes()),
        std::to_string(c.ways()),
        std::to_string(c.numSets()),
        organizationName(c),
        toString(c.replacement()),
        toString(c.writePolicy()),
        toString(c.writeAllocate()),
        std::to_string(s.accesses()),
        std::to_string(s.reads()),
        std::to_string(s.writes()),
        std::to_string(s.hits()),
        std::to_string(s.misses()),
        formatFixed(s.hitRate(), 6),
        formatFixed(s.missRate(), 6),
        std::to_string(s.readHits),
        std::to_string(s.readMisses),
        std::to_string(s.writeHits),
        std::to_string(s.writeMisses),
        std::to_string(s.evictions),
        std::to_string(s.dirtyEvictions),
        std::to_string(s.memoryReads),
        std::to_string(s.memoryWrites),
        std::to_string(result.l1.dirtyLinesResident),
    };

    if (result.l2) {
        const CacheConfig& c2 = result.l2->config;
        const Statistics& s2 = result.l2->stats;
        fields.push_back(std::to_string(c2.capacityBytes()));
        fields.push_back(std::to_string(c2.blockSizeBytes()));
        fields.push_back(std::to_string(c2.ways()));
        fields.push_back(std::to_string(s2.accesses()));
        fields.push_back(std::to_string(s2.hits()));
        fields.push_back(std::to_string(s2.misses()));
        fields.push_back(formatFixed(s2.hitRate(), 6));
        fields.push_back(std::to_string(s2.memoryReads));
        fields.push_back(std::to_string(s2.memoryWrites));
    } else {
        fields.insert(fields.end(), 9, std::string());  // l2_* columns left empty
    }

    fields.push_back(formatNumber(result.latency.l1HitCycles));
    fields.push_back(result.l2 ? formatNumber(result.latency.l2HitCycles) : std::string());
    fields.push_back(formatNumber(result.latency.memoryCycles));
    const std::optional<double> amat = estimatedAmat(result);
    fields.push_back(amat ? formatFixed(*amat, 6) : std::string());
    return joinCsv(fields);
}

std::string csvEscape(const std::string& field) {
    if (field.find_first_of(",\"\n\r") == std::string::npos) {
        return field;
    }
    std::string quoted = "\"";
    for (char c : field) {
        if (c == '"') quoted += '"';  // double any embedded quote
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

}  // namespace cachesim
