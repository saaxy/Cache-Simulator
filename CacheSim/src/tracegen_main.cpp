// cachesim_tracegen: write a synthetic, deterministic trace.
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "TraceGenerator.h"

int main(int argc, char** argv) {
    using namespace cachesim;
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        const GeneratorOptions options = parseGeneratorArguments(args);
        if (options.help) {
            std::cout << generatorUsageText();
            return 0;
        }
        const std::vector<TraceEntry> trace = generateTrace(options.spec);
        if (options.outPath.empty()) {
            writeTrace(std::cout, trace);
        } else {
            std::ofstream file(options.outPath);
            if (!file) {
                throw std::runtime_error("cannot open output file '" + options.outPath + "'");
            }
            writeTrace(file, trace);
        }
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "error: " << error.what() << "\nRun 'cachesim_tracegen --help' for usage.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
