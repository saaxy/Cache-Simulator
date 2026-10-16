#include "Simulation.h"

#include <stdexcept>
#include <string>

#include "Cache.h"

namespace cachesim {

namespace {

void validateHierarchy(const CacheConfig& l1, const CacheConfig& l2) {
    if (l2.blockSizeBytes() < l1.blockSizeBytes()) {
        throw std::invalid_argument("L2 block size (" + std::to_string(l2.blockSizeBytes()) +
                                    ") must be at least the L1 block size (" +
                                    std::to_string(l1.blockSizeBytes()) + ")");
    }
    if (l2.capacityBytes() < l1.capacityBytes()) {
        throw std::invalid_argument("L2 capacity (" + std::to_string(l2.capacityBytes()) +
                                    ") must be at least the L1 capacity (" +
                                    std::to_string(l1.capacityBytes()) + ")");
    }
}

void validateLatency(const LatencyModel& latency) {
    // Written so that NaN also fails.
    if (!(latency.l1HitCycles >= 0.0 && latency.l2HitCycles >= 0.0 &&
          latency.memoryCycles >= 0.0)) {
        throw std::invalid_argument("latencies must be non-negative numbers of cycles");
    }
}

}  // namespace

SimulationResult simulate(const CacheConfig& config, const std::vector<TraceEntry>& trace) {
    Cache cache(config);
    for (const TraceEntry& entry : trace) {
        cache.access(entry.type, entry.address);
    }
    return SimulationResult{config, cache.statistics(), cache.dirtyLineCount()};
}

RunResult runSimulation(const CacheConfig& l1Config, const std::optional<CacheConfig>& l2Config,
                        const LatencyModel& latency, const std::vector<TraceEntry>& trace) {
    validateLatency(latency);
    if (l2Config) {
        validateHierarchy(l1Config, *l2Config);
    }

    Cache l1(l1Config);
    std::optional<Cache> l2;
    if (l2Config) {
        l2.emplace(*l2Config);
    }

    for (const TraceEntry& entry : trace) {
        const AccessOutcome outcome = l1.accessDetailed(entry.type, entry.address);
        if (!l2) {
            continue;
        }
        if (outcome.dirtyEviction) {
            l2->access(AccessType::Write, outcome.evictedBlockAddress);
        }
        if (outcome.blockFetched) {
            l2->access(AccessType::Read, entry.address);
        }
        if (outcome.writeForwarded) {
            l2->access(AccessType::Write, entry.address);
        }
    }

    RunResult result{SimulationResult{l1Config, l1.statistics(), l1.dirtyLineCount()},
                     std::nullopt, latency};
    if (l2) {
        result.l2 = SimulationResult{*l2Config, l2->statistics(), l2->dirtyLineCount()};
    }
    return result;
}

std::optional<double> estimatedAmat(const RunResult& result) {
    const Statistics& l1 = result.l1.stats;
    if (l1.accesses() == 0) {
        return std::nullopt;
    }
    double missPenalty = result.latency.memoryCycles;
    if (result.l2) {
        // missRate() is 0 when L2 saw no requests, which is correct: then L1
        // never missed either.
        missPenalty = result.latency.l2HitCycles +
                      result.l2->stats.missRate() * result.latency.memoryCycles;
    }
    return result.latency.l1HitCycles + l1.missRate() * missPenalty;
}

}  // namespace cachesim
