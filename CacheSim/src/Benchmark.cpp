#include "Benchmark.h"

#include <cctype>
#include <functional>
#include <limits>
#include <stdexcept>

#include "CommandLine.h"
#include "Report.h"

namespace cachesim {

namespace {

std::string toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string formatSize(std::uint64_t bytes) {
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) {
        return std::to_string(bytes / (1024 * 1024)) + "M";
    }
    if (bytes >= 1024 && bytes % 1024 == 0) {
        return std::to_string(bytes / 1024) + "K";
    }
    return std::to_string(bytes);
}

CacheConfig makeL1(std::uint64_t capacity, std::uint64_t block, std::uint32_t ways,
                   ReplacementPolicy replacement = ReplacementPolicy::LRU,
                   WritePolicy write = WritePolicy::WriteBack,
                   WriteAllocatePolicy alloc = WriteAllocatePolicy::WriteAllocate) {
    return CacheConfig(capacity, block, ways, replacement, write, alloc);
}

}  // namespace

std::vector<Experiment> buildExperiments(std::uint64_t baseCapacity, std::uint64_t baseBlock,
                                         std::uint32_t baseWays) {
    const CacheConfig base = makeL1(baseCapacity, baseBlock, baseWays);  // throws if invalid
    std::vector<Experiment> experiments;

    // Adds the experiment if `build` yields a valid configuration; invalid
    // variants for this base are silently skipped.
    auto tryAdd = [&experiments](const std::function<Experiment()>& build) {
        try {
            experiments.push_back(build());
        } catch (const std::invalid_argument&) {
        }
    };

    // associativity
    for (std::uint32_t ways : {1u, 2u, 4u, 8u}) {
        tryAdd([&] {
            return Experiment{"associativity", std::to_string(ways) + "-way",
                              makeL1(baseCapacity, baseBlock, ways), std::nullopt};
        });
    }
    tryAdd([&] {
        return Experiment{"associativity", "full",
                          CacheConfig::fullyAssociative(baseCapacity, baseBlock,
                                                        ReplacementPolicy::LRU,
                                                        WritePolicy::WriteBack,
                                                        WriteAllocatePolicy::WriteAllocate),
                          std::nullopt};
    });

    // replacement
    tryAdd([&] {
        return Experiment{"replacement", "LRU",
                          makeL1(baseCapacity, baseBlock, baseWays, ReplacementPolicy::LRU),
                          std::nullopt};
    });
    tryAdd([&] {
        return Experiment{"replacement", "FIFO",
                          makeL1(baseCapacity, baseBlock, baseWays, ReplacementPolicy::FIFO),
                          std::nullopt};
    });

    // capacity
    for (std::uint64_t capacity : {1024ULL, 2048ULL, 4096ULL, 8192ULL, 16384ULL, 32768ULL,
                                   65536ULL}) {
        tryAdd([&] {
            return Experiment{"capacity", formatSize(capacity),
                              makeL1(capacity, baseBlock, baseWays), std::nullopt};
        });
    }

    // block size
    for (std::uint64_t block : {16ULL, 32ULL, 64ULL, 128ULL, 256ULL}) {
        tryAdd([&] {
            return Experiment{"block_size", std::to_string(block),
                              makeL1(baseCapacity, block, baseWays), std::nullopt};
        });
    }

    // write policies
    struct WriteCombo {
        WritePolicy write;
        WriteAllocatePolicy alloc;
    };
    for (const WriteCombo& combo :
         {WriteCombo{WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate},
          WriteCombo{WritePolicy::WriteBack, WriteAllocatePolicy::NoWriteAllocate},
          WriteCombo{WritePolicy::WriteThrough, WriteAllocatePolicy::WriteAllocate},
          WriteCombo{WritePolicy::WriteThrough, WriteAllocatePolicy::NoWriteAllocate}}) {
        const std::string label =
            std::string(toString(combo.write)) + "/" + toString(combo.alloc);
        tryAdd([&] {
            return Experiment{"write_policy", label,
                              makeL1(baseCapacity, baseBlock, baseWays, ReplacementPolicy::LRU,
                                     combo.write, combo.alloc),
                              std::nullopt};
        });
    }

    // L2 size sweep (the first entry is the single-level baseline)
    experiments.push_back(Experiment{"l2_size", "no-L2", base, std::nullopt});
    for (std::uint64_t l2Capacity : {16384ULL, 32768ULL, 65536ULL, 131072ULL, 262144ULL}) {
        tryAdd([&] {
            if (l2Capacity < baseCapacity) {
                throw std::invalid_argument("L2 smaller than L1");
            }
            return Experiment{"l2_size", formatSize(l2Capacity), base,
                              CacheConfig(l2Capacity, baseBlock, 8, ReplacementPolicy::LRU,
                                          WritePolicy::WriteBack,
                                          WriteAllocatePolicy::WriteAllocate)};
        });
    }
    return experiments;
}

std::string benchmarkCsvHeader() { return "experiment,variant," + csvHeader(); }

std::string benchmarkCsvRow(const Experiment& experiment, const RunResult& result,
                            const std::string& traceName) {
    return csvEscape(experiment.group) + "," + csvEscape(experiment.label) + "," +
           csvRow(result, traceName);
}

std::size_t runBenchmark(std::ostream& out, const std::vector<NamedTrace>& traces,
                         const std::vector<Experiment>& experiments,
                         const LatencyModel& latency) {
    out << benchmarkCsvHeader() << "\n";
    std::size_t rows = 0;
    for (const NamedTrace& trace : traces) {
        for (const Experiment& experiment : experiments) {
            const RunResult result =
                runSimulation(experiment.l1, experiment.l2, latency, trace.entries);
            out << benchmarkCsvRow(experiment, result, trace.name) << "\n";
            ++rows;
        }
    }
    return rows;
}

BenchmarkOptions parseBenchmarkArguments(const std::vector<std::string>& args) {
    BenchmarkOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string name = args[i];
        std::string inlineValue;
        bool hasInline = false;
        if (name.rfind("--", 0) == 0) {
            const std::size_t equals = name.find('=');
            if (equals != std::string::npos) {
                inlineValue = name.substr(equals + 1);
                name = name.substr(0, equals);
                hasInline = true;
            }
        }
        auto takeValue = [&]() -> std::string {
            if (hasInline) return inlineValue;
            if (i + 1 >= args.size()) {
                throw std::invalid_argument("option " + name + " requires a value");
            }
            return args[++i];
        };

        if (name == "--help" || name == "-h") {
            if (hasInline) throw std::invalid_argument("option " + name + " does not take a value");
            options.help = true;
        } else if (name == "--trace") {
            options.tracePaths.push_back(takeValue());
        } else if (name == "--synthetic") {
            const std::string value = takeValue();
            const std::vector<SyntheticKind> kinds = toLower(value) == "all"
                                                         ? allSyntheticKinds()
                                                         : std::vector<SyntheticKind>{
                                                               parseSyntheticKind(value)};
            for (SyntheticKind kind : kinds) {
                bool present = false;
                for (SyntheticKind existing : options.syntheticKinds) present |= (existing == kind);
                if (!present) options.syntheticKinds.push_back(kind);
            }
        } else if (name == "--out") {
            options.outPath = takeValue();
        } else if (name == "--base-size") {
            options.baseCapacity = parseSizeValue(takeValue(), name);
        } else if (name == "--base-block") {
            options.baseBlock = parseSizeValue(takeValue(), name);
        } else if (name == "--base-assoc") {
            const std::uint64_t ways = parseUnsignedValue(takeValue(), name);
            if (ways > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument(name + ": value is too large");
            }
            options.baseWays = static_cast<std::uint32_t>(ways);
        } else if (name == "--synthetic-count") {
            options.syntheticCount = parseUnsignedValue(takeValue(), name);
        } else if (name == "--seed") {
            options.seed = parseUnsignedValue(takeValue(), name);
        } else if (name == "--l1-latency") {
            options.latency.l1HitCycles = static_cast<double>(parseUnsignedValue(takeValue(), name));
        } else if (name == "--l2-latency") {
            options.latency.l2HitCycles = static_cast<double>(parseUnsignedValue(takeValue(), name));
        } else if (name == "--mem-latency") {
            options.latency.memoryCycles = static_cast<double>(parseUnsignedValue(takeValue(), name));
        } else {
            throw std::invalid_argument("unknown option '" + name + "'");
        }
    }
    if (!options.help && options.tracePaths.empty() && options.syntheticKinds.empty()) {
        throw std::invalid_argument("give at least one --trace FILE or --synthetic KIND");
    }
    return options;
}

std::string benchmarkUsageText() {
    return
        "Usage: cachesim_bench (--trace FILE | --synthetic KIND)... [options]\n"
        "\n"
        "Runs the standard parameter sweeps (associativity, replacement, capacity,\n"
        "block size, write policy, L2 size) on every trace and writes one CSV.\n"
        "\n"
        "Options:\n"
        "  --trace FILE          a trace file (repeatable)\n"
        "  --synthetic KIND      sequential | random | hotset | strided | all (repeatable)\n"
        "  --synthetic-count N   accesses per synthetic trace [50000]\n"
        "  --seed N              seed for synthetic traces [1]\n"
        "  --base-size BYTES     base L1 capacity for the sweeps [4K]\n"
        "  --base-block BYTES    base block size [64]\n"
        "  --base-assoc N        base associativity [4]\n"
        "  --l1-latency / --l2-latency / --mem-latency CYCLES\n"
        "                        model inputs for the AMAT estimate [4 / 12 / 200]\n"
        "  --out FILE            write the CSV to FILE instead of standard output\n"
        "  --help                show this message\n";
}

}  // namespace cachesim
