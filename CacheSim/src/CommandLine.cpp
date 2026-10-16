#include "CommandLine.h"

#include <cctype>
#include <limits>
#include <stdexcept>

namespace cachesim {

namespace {

std::string toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::uint64_t parseUnsignedImpl(const std::string& digits, const std::string& option) {
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    if (digits.empty()) {
        throw std::invalid_argument(option + " needs a number");
    }
    std::uint64_t value = 0;
    for (char c : digits) {
        if (!isDigit(c)) {
            throw std::invalid_argument(option + ": '" + digits +
                                        "' is not a non-negative integer");
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (kMax - digit) / 10) {
            throw std::invalid_argument(option + ": '" + digits + "' is too large");
        }
        value = value * 10 + digit;
    }
    return value;
}

// A byte count with an optional K/M/G suffix (powers of 1024), e.g. 32768, 32K, 1MB.
std::uint64_t parseSizeImpl(const std::string& text, const std::string& option) {
    std::size_t split = 0;
    while (split < text.size() && isDigit(text[split])) ++split;
    if (split == 0) {
        throw std::invalid_argument(option + ": '" + text + "' is not a valid size");
    }
    const std::uint64_t number = parseUnsignedImpl(text.substr(0, split), option);

    const std::string suffix = toLower(text.substr(split));
    std::uint64_t multiplier = 1;
    if (suffix.empty() || suffix == "b") {
        multiplier = 1;
    } else if (suffix == "k" || suffix == "kb") {
        multiplier = 1024ULL;
    } else if (suffix == "m" || suffix == "mb") {
        multiplier = 1024ULL * 1024;
    } else if (suffix == "g" || suffix == "gb") {
        multiplier = 1024ULL * 1024 * 1024;
    } else {
        throw std::invalid_argument(option + ": unknown size suffix '" + text.substr(split) +
                                    "' (use K, M or G)");
    }
    if (number > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        throw std::invalid_argument(option + ": '" + text + "' is too large");
    }
    return number * multiplier;
}

ReplacementPolicy parseReplacement(const std::string& text) {
    const std::string value = toLower(text);
    if (value == "lru") return ReplacementPolicy::LRU;
    if (value == "fifo") return ReplacementPolicy::FIFO;
    throw std::invalid_argument("--replacement: expected lru or fifo, got '" + text + "'");
}

WritePolicy parseWritePolicy(const std::string& text) {
    const std::string value = toLower(text);
    if (value == "wb" || value == "write-back") return WritePolicy::WriteBack;
    if (value == "wt" || value == "write-through") return WritePolicy::WriteThrough;
    throw std::invalid_argument("--write-policy: expected wb or wt, got '" + text + "'");
}

WriteAllocatePolicy parseWriteMiss(const std::string& text) {
    const std::string value = toLower(text);
    if (value == "allocate" || value == "write-allocate") {
        return WriteAllocatePolicy::WriteAllocate;
    }
    if (value == "no-allocate" || value == "no-write-allocate") {
        return WriteAllocatePolicy::NoWriteAllocate;
    }
    throw std::invalid_argument("--write-miss: expected allocate or no-allocate, got '" +
                                text + "'");
}

}  // namespace

Options parseArguments(const std::vector<std::string>& args) {
    Options options;
    bool l2TuningGiven = false;  // --l2-block / --l2-assoc / --l2-replacement seen
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string name = args[i];
        std::string inlineValue;
        bool hasInlineValue = false;
        if (name.rfind("--", 0) == 0) {
            const std::size_t equals = name.find('=');
            if (equals != std::string::npos) {
                inlineValue = name.substr(equals + 1);
                name = name.substr(0, equals);
                hasInlineValue = true;
            }
        }

        // Fetches the option's value from "--opt=value" or the next argument.
        auto takeValue = [&]() -> std::string {
            if (hasInlineValue) return inlineValue;
            if (i + 1 >= args.size()) {
                throw std::invalid_argument("option " + name + " requires a value");
            }
            return args[++i];
        };
        auto rejectValue = [&]() {
            if (hasInlineValue) {
                throw std::invalid_argument("option " + name + " does not take a value");
            }
        };

        if (name == "--help" || name == "-h") {
            rejectValue();
            options.help = true;
        } else if (name == "--csv") {
            rejectValue();
            options.csv = true;
        } else if (name == "--no-header") {
            rejectValue();
            options.csvHeader = false;
        } else if (name == "--trace") {
            options.tracePath = takeValue();
        } else if (name == "--size") {
            options.capacityBytes = parseSizeImpl(takeValue(), name);
        } else if (name == "--block") {
            options.blockSizeBytes = parseSizeImpl(takeValue(), name);
        } else if (name == "--assoc") {
            const std::string value = takeValue();
            const std::string lower = toLower(value);
            if (lower == "full" || lower == "fully") {
                options.fullyAssociative = true;
            } else {
                const std::uint64_t ways = parseUnsignedImpl(value, name);
                if (ways > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::invalid_argument(name + ": '" + value + "' is too large");
                }
                options.fullyAssociative = false;
                options.ways = static_cast<std::uint32_t>(ways);
            }
        } else if (name == "--replacement") {
            options.replacement = parseReplacement(takeValue());
        } else if (name == "--write-policy") {
            options.writePolicy = parseWritePolicy(takeValue());
        } else if (name == "--write-miss") {
            options.writeAllocate = parseWriteMiss(takeValue());
        } else if (name == "--l2-size") {
            options.hasL2 = true;
            options.l2CapacityBytes = parseSizeImpl(takeValue(), name);
        } else if (name == "--l2-block") {
            l2TuningGiven = true;
            options.l2BlockSizeBytes = parseSizeImpl(takeValue(), name);
        } else if (name == "--l2-assoc") {
            l2TuningGiven = true;
            const std::string value = takeValue();
            const std::string lower = toLower(value);
            if (lower == "full" || lower == "fully") {
                options.l2FullyAssociative = true;
            } else {
                const std::uint64_t ways = parseUnsignedImpl(value, name);
                if (ways > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::invalid_argument(name + ": '" + value + "' is too large");
                }
                options.l2FullyAssociative = false;
                options.l2Ways = static_cast<std::uint32_t>(ways);
            }
        } else if (name == "--l2-replacement") {
            l2TuningGiven = true;
            options.l2Replacement = parseReplacement(takeValue());
        } else if (name == "--l1-latency") {
            options.latency.l1HitCycles = static_cast<double>(parseUnsignedImpl(takeValue(), name));
        } else if (name == "--l2-latency") {
            options.latency.l2HitCycles = static_cast<double>(parseUnsignedImpl(takeValue(), name));
        } else if (name == "--mem-latency") {
            options.latency.memoryCycles = static_cast<double>(parseUnsignedImpl(takeValue(), name));
        } else {
            throw std::invalid_argument("unknown option '" + name + "'");
        }
    }

    if (!options.help && options.tracePath.empty()) {
        throw std::invalid_argument("missing required option --trace FILE");
    }
    if (l2TuningGiven && !options.hasL2) {
        throw std::invalid_argument(
            "--l2-block, --l2-assoc and --l2-replacement need --l2-size to enable an L2");
    }
    return options;
}

CacheConfig makeCacheConfig(const Options& options) {
    if (options.fullyAssociative) {
        return CacheConfig::fullyAssociative(options.capacityBytes, options.blockSizeBytes,
                                             options.replacement, options.writePolicy,
                                             options.writeAllocate);
    }
    return CacheConfig(options.capacityBytes, options.blockSizeBytes, options.ways,
                       options.replacement, options.writePolicy, options.writeAllocate);
}

std::optional<CacheConfig> makeL2Config(const Options& options) {
    if (!options.hasL2) {
        return std::nullopt;
    }
    const std::uint64_t block =
        options.l2BlockSizeBytes == 0 ? options.blockSizeBytes : options.l2BlockSizeBytes;
    if (options.l2FullyAssociative) {
        return CacheConfig::fullyAssociative(options.l2CapacityBytes, block,
                                             options.l2Replacement, WritePolicy::WriteBack,
                                             WriteAllocatePolicy::WriteAllocate);
    }
    return CacheConfig(options.l2CapacityBytes, block, options.l2Ways, options.l2Replacement,
                       WritePolicy::WriteBack, WriteAllocatePolicy::WriteAllocate);
}

std::uint64_t parseSizeValue(const std::string& text, const std::string& option) {
    return parseSizeImpl(text, option);
}

std::uint64_t parseUnsignedValue(const std::string& text, const std::string& option) {
    return parseUnsignedImpl(text, option);
}

std::string usageText() {
    return
        "Usage: cachesim --trace FILE [options]\n"
        "\n"
        "Options:\n"
        "  --trace FILE            trace file, one access per line: R|W 0xADDRESS (required)\n"
        "  --size BYTES            cache capacity, power of two; suffix K, M, G ok [32K]\n"
        "  --block BYTES           block size, power of two [64]\n"
        "  --assoc N|full          ways per set (1 = direct-mapped) or 'full' [4]\n"
        "  --replacement lru|fifo  replacement policy [lru]\n"
        "  --write-policy wb|wt    write-back or write-through [wb]\n"
        "  --write-miss allocate|no-allocate\n"
        "                          write-allocate or no-write-allocate [allocate]\n"
        "  --l2-size BYTES         add an L2 cache of this size (write-back, write-allocate)\n"
        "  --l2-block BYTES        L2 block size, >= L1 block size [same as L1]\n"
        "  --l2-assoc N|full       L2 ways per set [8]\n"
        "  --l2-replacement lru|fifo  L2 replacement policy [lru]\n"
        "  --l1-latency CYCLES     L1 hit latency for the AMAT estimate [4]\n"
        "  --l2-latency CYCLES     L2 hit latency for the AMAT estimate [12]\n"
        "  --mem-latency CYCLES    memory latency for the AMAT estimate [200]\n"
        "                          (latencies are illustrative model inputs, not measurements)\n"
        "  --csv                   print one CSV row (with header) instead of the report\n"
        "  --no-header             with --csv, omit the header line\n"
        "  --help                  show this message\n";
}

}  // namespace cachesim
