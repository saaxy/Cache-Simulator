#include "TraceGenerator.h"

#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>

#include "CommandLine.h"

namespace cachesim {

namespace {

std::string toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

void validate(const SyntheticSpec& spec) {
    if (spec.writePercent > 100) {
        throw std::invalid_argument("write percent must be between 0 and 100");
    }
    const bool usesStride =
        spec.kind == SyntheticKind::Sequential || spec.kind == SyntheticKind::Strided;
    const bool usesHot = spec.kind == SyntheticKind::HotSet || spec.kind == SyntheticKind::Strided;
    const bool usesRange = spec.kind != SyntheticKind::Strided;
    if (usesStride && spec.strideBytes == 0) {
        throw std::invalid_argument("stride must be at least 1 byte");
    }
    if (usesHot && spec.hotBlocks == 0) {
        throw std::invalid_argument("hot blocks must be at least 1");
    }
    if (usesRange && spec.rangeBytes < 64) {
        throw std::invalid_argument("range must be at least 64 bytes");
    }
    if (spec.kind == SyntheticKind::Strided &&
        spec.hotBlocks > std::numeric_limits<std::uint64_t>::max() / spec.strideBytes) {
        throw std::invalid_argument("hot blocks x stride does not fit in 64 bits");
    }
}

}  // namespace

const char* toString(SyntheticKind kind) {
    switch (kind) {
        case SyntheticKind::Sequential: return "sequential";
        case SyntheticKind::Random: return "random";
        case SyntheticKind::HotSet: return "hotset";
        case SyntheticKind::Strided: return "strided";
    }
    return "unknown";
}

SyntheticKind parseSyntheticKind(const std::string& text) {
    const std::string value = toLower(text);
    if (value == "sequential") return SyntheticKind::Sequential;
    if (value == "random") return SyntheticKind::Random;
    if (value == "hotset") return SyntheticKind::HotSet;
    if (value == "strided") return SyntheticKind::Strided;
    throw std::invalid_argument("unknown synthetic kind '" + text +
                                "' (expected sequential, random, hotset or strided)");
}

std::vector<SyntheticKind> allSyntheticKinds() {
    return {SyntheticKind::Sequential, SyntheticKind::Random, SyntheticKind::HotSet,
            SyntheticKind::Strided};
}

SyntheticSpec defaultSpec(SyntheticKind kind, std::uint64_t count, std::uint64_t seed) {
    SyntheticSpec spec;
    spec.kind = kind;
    spec.count = count;
    spec.seed = seed;
    switch (kind) {
        case SyntheticKind::Sequential:
            spec.rangeBytes = 16 * 1024;
            spec.strideBytes = 4;
            break;
        case SyntheticKind::Random:
            spec.rangeBytes = 64 * 1024;
            break;
        case SyntheticKind::HotSet:
            spec.rangeBytes = 64 * 1024;
            spec.hotBlocks = 32;
            break;
        case SyntheticKind::Strided:
            spec.strideBytes = 4096;
            spec.hotBlocks = 8;
            spec.rangeBytes = spec.strideBytes * spec.hotBlocks;
            break;
    }
    return spec;
}

std::vector<TraceEntry> generateTrace(const SyntheticSpec& spec) {
    validate(spec);
    std::mt19937_64 rng(spec.seed);

    std::vector<std::uint64_t> hot;  // HotSet: base addresses of the hot regions
    if (spec.kind == SyntheticKind::HotSet) {
        for (std::uint64_t i = 0; i < spec.hotBlocks; ++i) {
            hot.push_back((rng() % (spec.rangeBytes / 64)) * 64);
        }
    }

    std::vector<TraceEntry> trace;
    trace.reserve(static_cast<std::size_t>(spec.count));
    std::uint64_t offset = 0;  // Sequential: current position, kept below rangeBytes
    for (std::uint64_t i = 0; i < spec.count; ++i) {
        std::uint64_t address = 0;
        switch (spec.kind) {
            case SyntheticKind::Sequential:
                address = offset;
                offset = (offset + spec.strideBytes % spec.rangeBytes) % spec.rangeBytes;
                break;
            case SyntheticKind::Random:
                address = (rng() % (spec.rangeBytes / 4)) * 4;
                break;
            case SyntheticKind::HotSet:
                address = hot[rng() % hot.size()] + (rng() % 16) * 4;
                break;
            case SyntheticKind::Strided:
                address = (i % spec.hotBlocks) * spec.strideBytes;
                break;
        }
        const bool isWrite = (rng() % 100) < spec.writePercent;
        trace.push_back({isWrite ? AccessType::Write : AccessType::Read, address});
    }
    return trace;
}

void writeTrace(std::ostream& out, const std::vector<TraceEntry>& trace) {
    for (const TraceEntry& entry : trace) {
        out << (entry.type == AccessType::Read ? 'R' : 'W') << " 0x" << std::hex
            << std::uppercase << std::setw(8) << std::setfill('0') << entry.address << std::dec
            << std::setfill(' ') << '\n';
    }
}

GeneratorOptions parseGeneratorArguments(const std::vector<std::string>& args) {
    GeneratorOptions options;
    std::optional<SyntheticKind> kind;
    std::optional<std::uint64_t> count, seed, range, stride, hot, writePercent;

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
        } else if (name == "--kind") {
            kind = parseSyntheticKind(takeValue());
        } else if (name == "--count") {
            count = parseUnsignedValue(takeValue(), name);
        } else if (name == "--seed") {
            seed = parseUnsignedValue(takeValue(), name);
        } else if (name == "--range") {
            range = parseSizeValue(takeValue(), name);
        } else if (name == "--stride") {
            stride = parseSizeValue(takeValue(), name);
        } else if (name == "--hot-blocks") {
            hot = parseUnsignedValue(takeValue(), name);
        } else if (name == "--write-percent") {
            writePercent = parseUnsignedValue(takeValue(), name);
        } else if (name == "--out") {
            options.outPath = takeValue();
        } else {
            throw std::invalid_argument("unknown option '" + name + "'");
        }
    }

    if (options.help) {
        return options;
    }
    if (!kind) {
        throw std::invalid_argument("missing required option --kind");
    }
    options.spec = defaultSpec(*kind);
    if (count) options.spec.count = *count;
    if (seed) options.spec.seed = *seed;
    if (range) options.spec.rangeBytes = *range;
    if (stride) options.spec.strideBytes = *stride;
    if (hot) options.spec.hotBlocks = *hot;
    if (writePercent) {
        if (*writePercent > 100) {
            throw std::invalid_argument("--write-percent must be between 0 and 100");
        }
        options.spec.writePercent = static_cast<std::uint32_t>(*writePercent);
    }
    validate(options.spec);
    return options;
}

std::string generatorUsageText() {
    return
        "Usage: cachesim_tracegen --kind KIND [options]\n"
        "\n"
        "Writes a synthetic, deterministic trace (same options + seed = same trace).\n"
        "\n"
        "Options:\n"
        "  --kind KIND          sequential | random | hotset | strided (required)\n"
        "  --count N            number of accesses [50000]\n"
        "  --seed N             random seed [1]\n"
        "  --range BYTES        address range (sequential/random/hotset); K, M, G ok\n"
        "  --stride BYTES       step for sequential / strided\n"
        "  --hot-blocks N       distinct addresses for hotset / strided\n"
        "  --write-percent P    percentage of accesses that are writes [20]\n"
        "  --out FILE           write to FILE instead of standard output\n"
        "  --help               show this message\n";
}

}  // namespace cachesim
