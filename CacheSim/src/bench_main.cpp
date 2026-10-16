// cachesim_bench: run the standard experiment sweeps on one or more traces.
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Benchmark.h"
#include "TraceGenerator.h"
#include "TraceParser.h"

int main(int argc, char** argv) {
    using namespace cachesim;
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        const BenchmarkOptions options = parseBenchmarkArguments(args);
        if (options.help) {
            std::cout << benchmarkUsageText();
            return 0;
        }

        std::vector<NamedTrace> traces;
        for (const std::string& path : options.tracePaths) {
            traces.push_back({path, parseTraceFile(path)});
        }
        for (SyntheticKind kind : options.syntheticKinds) {
            const SyntheticSpec spec = defaultSpec(kind, options.syntheticCount, options.seed);
            traces.push_back({std::string("synthetic:") + toString(kind), generateTrace(spec)});
        }

        const std::vector<Experiment> experiments =
            buildExperiments(options.baseCapacity, options.baseBlock, options.baseWays);

        std::size_t rows = 0;
        if (options.outPath.empty()) {
            rows = runBenchmark(std::cout, traces, experiments, options.latency);
        } else {
            std::ofstream file(options.outPath);
            if (!file) {
                throw std::runtime_error("cannot open output file '" + options.outPath + "'");
            }
            rows = runBenchmark(file, traces, experiments, options.latency);
            std::cerr << "wrote " << rows << " rows to " << options.outPath << "\n";
        }
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "error: " << error.what() << "\nRun 'cachesim_bench --help' for usage.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
