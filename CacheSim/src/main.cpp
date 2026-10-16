// Command-line driver: parse options, load the trace, simulate, report.
// All simulation logic lives in the cachesim_core library.
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "CommandLine.h"
#include "Report.h"
#include "Simulation.h"
#include "TraceParser.h"

int main(int argc, char** argv) {
    using namespace cachesim;
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        const Options options = parseArguments(args);
        if (options.help) {
            std::cout << usageText();
            return 0;
        }

        const CacheConfig l1Config = makeCacheConfig(options);
        const std::optional<CacheConfig> l2Config = makeL2Config(options);
        const std::vector<TraceEntry> trace = parseTraceFile(options.tracePath);
        const RunResult result = runSimulation(l1Config, l2Config, options.latency, trace);

        if (options.csv) {
            if (options.csvHeader) {
                std::cout << csvHeader() << "\n";
            }
            std::cout << csvRow(result, options.tracePath) << "\n";
        } else {
            printTextReport(std::cout, result, options.tracePath);
        }
        return 0;
    } catch (const std::invalid_argument& error) {
        // Bad option, invalid cache geometry, or malformed trace line.
        std::cerr << "error: " << error.what() << "\n"
                  << "Run 'cachesim --help' for usage.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
