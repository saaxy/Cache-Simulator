#pragma once

namespace cachesim {

// Latencies, in cycles, used ONLY for the model-based AMAT estimate.
// The defaults are illustrative round numbers, not measurements of any real
// processor. Change them with --l1-latency / --l2-latency / --mem-latency.
struct LatencyModel {
    double l1HitCycles = 4.0;
    double l2HitCycles = 12.0;
    double memoryCycles = 200.0;
};

}  // namespace cachesim
