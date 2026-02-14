#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcParser.hpp"
#include "ntrak/nspc/NspcProjectFile.hpp"
#include "ntrak/emulation/SpcDsp.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ntrak::tools {
namespace {

constexpr size_t kSpcHeaderSize = 0x100;
constexpr size_t kSpcAramSize = 0x10000;
constexpr size_t kSpcDspRegOffset = kSpcHeaderSize + kSpcAramSize;
constexpr size_t kSpcDspRegSize = 128;
constexpr size_t kSpcMinimumSize = kSpcDspRegOffset + kSpcDspRegSize;
constexpr size_t kSpcExtraRamOffset = 0x101C0;
constexpr size_t kSpcExtraRamSize = 0x40;
constexpr size_t kSpcMinimumSizeWithExtraRam = kSpcExtraRamOffset + kSpcExtraRamSize;

constexpr size_t kSpcPcOffset = 0x25;
constexpr size_t kSpcAOffset = 0x27;
constexpr size_t kSpcXOffset = 0x28;
constexpr size_t kSpcYOffset = 0x29;
constexpr size_t kSpcPsOffset = 0x2A;
constexpr size_t kSpcSpOffset = 0x2B;

enum class DumpVariant : uint8_t {
    Baseline,
    Flattened,
    Optimized,
    FlatOptimized,
};

struct ToolOptions {
    std::optional<std::filesystem::path> overlayPath;
    std::optional<std::filesystem::path> spcPath;
    std::optional<std::filesystem::path> baseSpcPathOverride;
    std::filesystem::path outputDir = "song_dump";
    int songIndex = 0;
    std::vector<DumpVariant> variants;
    bool emitSpc = false;
    std::optional<uint8_t> triggerPortOverride = std::nullopt;
};

struct LoadedProjectContext {
    nspc::NspcProject project;
    std::filesystem::path sourcePath;
    std::filesystem::path sourceSpcPath;
    bool loadedFromOverlay = false;
};

struct VariantContext {
    DumpVariant variant = DumpVariant::Baseline;
    nspc::NspcProject project;
    nspc::NspcSong song;
    nspc::NspcCompileOutput compileOutput;
    nspc::NspcSongAddressLayout layout;
};

[[nodiscard]] std::string parseErrorToString(nspc::NspcParseError error) {
    switch (error) {
    case nspc::NspcParseError::InvalidConfig:
        return "Invalid engine configuration";
    case nspc::NspcParseError::InvalidHeader:
        return "File is not a valid SPC";
    case nspc::NspcParseError::UnsupportedVersion:
        return "SPC engine is not recognized by current engine configs";
    case nspc::NspcParseError::UnexpectedEndOfData:
        return "SPC file is truncated";
    case nspc::NspcParseError::InvalidEventData:
        return "SPC contains invalid event data";
    default:
        return "Unknown SPC parse error";
    }
}

[[nodiscard]] std::string variantName(DumpVariant variant) {
    switch (variant) {
    case DumpVariant::Baseline:
        return "baseline";
    case DumpVariant::Flattened:
        return "flattened";
    case DumpVariant::Optimized:
        return "optimized";
    case DumpVariant::FlatOptimized:
        return "flat_optimized";
    }
    return "unknown";
}

[[nodiscard]] std::optional<DumpVariant> parseVariant(std::string_view value) {
    if (value == "baseline" || value == "unoptimized") {
        return DumpVariant::Baseline;
    }
    if (value == "flattened") {
        return DumpVariant::Flattened;
    }
    if (value == "optimized") {
        return DumpVariant::Optimized;
    }
    if (value == "flat_optimized" || value == "flat-optimized") {
        return DumpVariant::FlatOptimized;
    }
    return std::nullopt;
}

void printUsage(std::ostream& out, std::string_view programName) {
    out << "Usage:\n";
    out << "  " << programName
        << " (--project <file.ntrakproj> [--base-spc <file.spc>] | --spc <file.spc>) [--song-index <n>] [--out-dir <dir>] [--variant <name>]\n";
    out << "\nOptions:\n";
    out << "  --project, -p   Path to .ntrakproj overlay file\n";
    out << "  --spc, -s       Path to SPC file (no project overlay)\n";
    out << "  --base-spc      Optional override for base SPC path (project mode only)\n";
    out << "  --song-index    Song index to dump (default: 0)\n";
    out << "  --trigger-port  Override song trigger port (0-3) for emitted SPC startup state\n";
    out << "  --out-dir       Output directory (default: song_dump)\n";
    out << "  --variant       baseline|unoptimized | flattened | optimized | flat_optimized | all (can be repeated)\n";
    out << "                  Defaults: project mode = baseline+flattened+optimized; spc mode = baseline+flat_optimized\n";
    out << "  --emit-spc      Write a patched SPC for each variant with playback state reinitialized\n";
    out << "  --help, -h      Show this help\n";
}

std::expected<std::vector<uint8_t>, std::string> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Failed to open '{}'", path.string()));
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

std::expected<void, std::string> writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(std::format("Failed to open '{}' for writing", path.string()));
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) {
        return std::unexpected(std::format("Failed while writing '{}'", path.string()));
    }
    return {};
}

std::expected<void, std::string> writeBinaryFile(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(std::format("Failed to open '{}' for writing", path.string()));
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out.good()) {
        return std::unexpected(std::format("Failed while writing '{}'", path.string()));
    }
    return {};
}

std::expected<std::vector<uint8_t>, std::string> buildDebugPlaybackSpc(std::span<const uint8_t> spcImage,
                                                                        const nspc::NspcEngineConfig& engine,
                                                                        int songIndex,
                                                                        std::optional<uint8_t> triggerPortOverride,
                                                                        std::string* outStateSummary = nullptr) {
    if (spcImage.size() < kSpcMinimumSize) {
        return std::unexpected("SPC image is too small to rewrite playback state");
    }

    emulation::SpcDsp dsp;
    if (!dsp.loadSpcFile(spcImage.data(), static_cast<uint32_t>(spcImage.size()))) {
        return std::unexpected("Failed to load patched SPC into emulator while preparing playback snapshot");
    }

    // Mirror app playback startup so external emulators begin from a stable song-start state.
    // Do not call reset() here; it would wipe the uploaded SPC image state.
    dsp.setPC(engine.entryPoint);
    constexpr uint64_t kEngineWarmupCycles = 140000;
    dsp.runCycles(kEngineWarmupCycles);

    const uint8_t configuredTriggerPort = static_cast<uint8_t>(engine.songTriggerPort & 0x03u);
    const uint8_t triggerPort = triggerPortOverride.value_or(configuredTriggerPort);
    const uint8_t triggerValue =
        static_cast<uint8_t>((static_cast<uint32_t>(songIndex) + engine.songTriggerOffset) & 0xFFu);
    dsp.writePort(triggerPort, triggerValue);
    // Let the engine consume the trigger before capturing snapshot state.
    constexpr uint64_t kPostTriggerSettleCycles = 12000;
    dsp.runCycles(kPostTriggerSettleCycles);

    std::vector<uint8_t> output(spcImage.begin(), spcImage.end());
    if (output.size() < kSpcMinimumSizeWithExtraRam) {
        output.resize(kSpcMinimumSizeWithExtraRam, 0);
    }

    const auto aramView = dsp.aram();
    const auto aramBytes = aramView.all();
    std::copy(aramBytes.begin(), aramBytes.end(), output.begin() + static_cast<ptrdiff_t>(kSpcHeaderSize));
    // Mirror trigger port into $F4-$F7 for deterministic startup when loaded by external players.
    output[kSpcHeaderSize + 0xF4 + triggerPort] = triggerValue;

    for (size_t i = 0; i < kSpcDspRegSize; ++i) {
        output[kSpcDspRegOffset + i] = dsp.readDspRegister(static_cast<uint8_t>(i));
    }

    std::copy_n(aramBytes.begin() + static_cast<ptrdiff_t>(0xFFC0), static_cast<ptrdiff_t>(kSpcExtraRamSize),
                output.begin() + static_cast<ptrdiff_t>(kSpcExtraRamOffset));

    const uint16_t pc = dsp.pc();
    output[kSpcPcOffset] = static_cast<uint8_t>(pc & 0xFFu);
    output[kSpcPcOffset + 1] = static_cast<uint8_t>((pc >> 8u) & 0xFFu);
    output[kSpcAOffset] = dsp.a();
    output[kSpcXOffset] = dsp.x();
    output[kSpcYOffset] = dsp.y();
    output[kSpcPsOffset] = dsp.ps();
    output[kSpcSpOffset] = dsp.sp();

    // Keep ARAM $F0-$FF as captured; ioState() is partially synthetic and not authoritative.
    const emulation::SpcIoState ioState = dsp.ioState();

    if (outStateSummary != nullptr) {
        *outStateSummary = std::format(
            "Engine entry: ${:04X}\n"
            "Song index: {}\n"
            "Trigger port (configured): {}\n"
            "Trigger port (used): {}\n"
            "Trigger offset: ${:02X}\n"
            "Trigger value: ${:02X}\n"
            "CPU: PC=${:04X} A=${:02X} X=${:02X} Y=${:02X} PS=${:02X} SP=${:02X}\n"
            "CPU input ports: F4=${:02X} F5=${:02X} F6=${:02X} F7=${:02X}\n"
            "CPU output ports: O0=${:02X} O1=${:02X} O2=${:02X} O3=${:02X}\n"
            "SPC I/O: F0=${:02X} F1=${:02X} F2=${:02X} F3=${:02X} F8=${:02X} F9=${:02X}\n"
            "Timers: FA=${:02X} FB=${:02X} FC=${:02X} FD=${:02X} FE=${:02X} FF=${:02X}\n"
            "Extra RAM: [101C0..101FF] copied from ARAM[FFC0..FFFF]\n",
            engine.entryPoint, songIndex, configuredTriggerPort, triggerPort, engine.songTriggerOffset, triggerValue,
            dsp.pc(), dsp.a(), dsp.x(),
            dsp.y(), dsp.ps(), dsp.sp(), ioState.cpuInputRegs[0], ioState.cpuInputRegs[1], ioState.cpuInputRegs[2],
            ioState.cpuInputRegs[3], ioState.cpuOutputRegs[0], ioState.cpuOutputRegs[1], ioState.cpuOutputRegs[2],
            ioState.cpuOutputRegs[3], ioState.testReg, ioState.controlReg, ioState.dspRegSelect,
            output[kSpcHeaderSize + 0xF3], ioState.ramRegs[0], ioState.ramRegs[1], ioState.timerTargets[0],
            ioState.timerTargets[1], ioState.timerTargets[2], ioState.timerOutputs[0], ioState.timerOutputs[1],
            ioState.timerOutputs[2]);
    }

    return output;
}

std::expected<ToolOptions, std::string> parseArgs(int argc, char** argv) {
    ToolOptions options;
    bool variantExplicitlySet = false;
    bool hasOverlay = false;
    bool hasSpc = false;

    auto require_value = [&](int& index, std::string_view flag) -> std::expected<std::string, std::string> {
        if (index + 1 >= argc) {
            return std::unexpected(std::format("Missing value for {}", flag));
        }
        ++index;
        return std::string(argv[index]);
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(std::cout, argc > 0 ? argv[0] : "ntrak_song_dump");
            std::exit(0);
        }
        if (arg == "--project" || arg == "-p") {
            auto value = require_value(i, arg);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            options.overlayPath = *value;
            hasOverlay = true;
            continue;
        }
        if (arg == "--spc" || arg == "-s") {
            auto value = require_value(i, arg);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            options.spcPath = *value;
            hasSpc = true;
            continue;
        }
        if (arg == "--base-spc") {
            auto value = require_value(i, arg);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            options.baseSpcPathOverride = std::filesystem::path(*value);
            continue;
        }
        if (arg == "--song-index") {
            auto value = require_value(i, arg);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            try {
                options.songIndex = std::stoi(*value);
            } catch (...) {
                return std::unexpected(std::format("Invalid --song-index value '{}'", *value));
            }
            continue;
        }
        if (arg == "--trigger-port") {
            auto value = require_value(i, arg);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            int parsedPort = -1;
            try {
                parsedPort = std::stoi(*value);
            } catch (...) {
                return std::unexpected(std::format("Invalid --trigger-port value '{}'", *value));
            }
            if (parsedPort < 0 || parsedPort > 3) {
                return std::unexpected(std::format("--trigger-port must be in range 0-3 (got '{}')", *value));
            }
            options.triggerPortOverride = static_cast<uint8_t>(parsedPort);
            continue;
        }
        if (arg == "--out-dir") {
            auto value = require_value(i, arg);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            options.outputDir = *value;
            continue;
        }
        if (arg == "--variant") {
            auto value = require_value(i, arg);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            if (!variantExplicitlySet) {
                options.variants.clear();
                variantExplicitlySet = true;
            }
            if (*value == "all") {
                options.variants = {DumpVariant::Baseline, DumpVariant::Flattened, DumpVariant::Optimized,
                                    DumpVariant::FlatOptimized};
                continue;
            }
            auto parsed = parseVariant(*value);
            if (!parsed.has_value()) {
                return std::unexpected(
                    std::format(
                        "Invalid --variant '{}': expected baseline|unoptimized|flattened|optimized|flat_optimized|all",
                        *value));
            }
            options.variants.push_back(*parsed);
            continue;
        }
        if (arg == "--emit-spc" || arg == "--write-spc") {
            options.emitSpc = true;
            continue;
        }
        if (arg.starts_with("-")) {
            return std::unexpected(std::format("Unknown option '{}'", arg));
        }
        if (!hasOverlay && !hasSpc) {
            const std::filesystem::path positionalPath(arg);
            if (positionalPath.extension() == ".spc" || positionalPath.extension() == ".SPC") {
                options.spcPath = positionalPath;
                hasSpc = true;
            } else {
                options.overlayPath = positionalPath;
                hasOverlay = true;
            }
            continue;
        }
        return std::unexpected(std::format("Unexpected positional argument '{}'", arg));
    }

    if (hasOverlay && hasSpc) {
        return std::unexpected("Pass either --project or --spc, not both");
    }
    if (!hasOverlay && !hasSpc) {
        return std::unexpected("Missing required input. Use --project <file.ntrakproj> or --spc <file.spc>");
    }
    if (hasSpc && options.baseSpcPathOverride.has_value()) {
        return std::unexpected("--base-spc is only valid with --project mode");
    }
    if (options.songIndex < 0) {
        return std::unexpected("--song-index must be >= 0");
    }
    if (!variantExplicitlySet) {
        if (hasSpc) {
            options.variants = {DumpVariant::Baseline, DumpVariant::FlatOptimized};
        } else {
            options.variants = {DumpVariant::Baseline, DumpVariant::Flattened, DumpVariant::Optimized};
        }
    }
    if (options.variants.empty()) {
        return std::unexpected("No variants selected");
    }

    // Deduplicate while preserving first occurrence order.
    std::vector<DumpVariant> deduped;
    for (const auto variant : options.variants) {
        if (std::find(deduped.begin(), deduped.end(), variant) == deduped.end()) {
            deduped.push_back(variant);
        }
    }
    options.variants = std::move(deduped);
    return options;
}

std::expected<nspc::NspcProject, std::string> loadProjectFromSpc(const std::filesystem::path& spcPath) {
    auto spcData = readBinaryFile(spcPath);
    if (!spcData.has_value()) {
        return std::unexpected(spcData.error());
    }

    auto parsedProject = nspc::NspcParser::load(*spcData);
    if (!parsedProject.has_value()) {
        return std::unexpected(
            std::format("Failed to parse SPC '{}': {}", spcPath.string(), parseErrorToString(parsedProject.error())));
    }

    return *std::move(parsedProject);
}

std::expected<LoadedProjectContext, std::string> loadProject(const ToolOptions& options) {
    if (options.spcPath.has_value()) {
        const auto& spcPath = *options.spcPath;
        std::error_code existsError;
        if (!std::filesystem::exists(spcPath, existsError) || existsError) {
            return std::unexpected(std::format("SPC does not exist: '{}'", spcPath.string()));
        }

        auto project = loadProjectFromSpc(spcPath);
        if (!project.has_value()) {
            return std::unexpected(project.error());
        }

        return LoadedProjectContext{
            .project = std::move(*project),
            .sourcePath = spcPath,
            .sourceSpcPath = spcPath,
            .loadedFromOverlay = false,
        };
    }

    if (!options.overlayPath.has_value()) {
        return std::unexpected("Internal error: expected overlay path");
    }

    const auto overlayData = nspc::loadProjectIrFile(*options.overlayPath);
    if (!overlayData.has_value()) {
        return std::unexpected(std::format("Failed to load project file: {}", overlayData.error()));
    }

    std::filesystem::path baseSpcPath;
    if (options.baseSpcPathOverride.has_value()) {
        baseSpcPath = *options.baseSpcPathOverride;
    } else if (overlayData->baseSpcPath.has_value()) {
        baseSpcPath = *overlayData->baseSpcPath;
        if (baseSpcPath.is_relative()) {
            baseSpcPath = options.overlayPath->parent_path() / baseSpcPath;
        }
    } else {
        return std::unexpected(
            "Project file does not contain baseSpcPath; pass one with --base-spc <file.spc>");
    }

    std::error_code existsError;
    if (!std::filesystem::exists(baseSpcPath, existsError) || existsError) {
        return std::unexpected(std::format("Base SPC does not exist: '{}'", baseSpcPath.string()));
    }

    auto project = loadProjectFromSpc(baseSpcPath);
    if (!project.has_value()) {
        return std::unexpected(project.error());
    }

    auto applyResult = nspc::applyProjectIrOverlay(*project, *overlayData);
    if (!applyResult.has_value()) {
        return std::unexpected(std::format("Failed to apply overlay: {}", applyResult.error()));
    }

    return LoadedProjectContext{
        .project = std::move(*project),
        .sourcePath = *options.overlayPath,
        .sourceSpcPath = std::move(baseSpcPath),
        .loadedFromOverlay = true,
    };
}

[[nodiscard]] std::string hexDump(std::span<const uint8_t> bytes, size_t columns = 16) {
    std::string out;
    for (size_t offset = 0; offset < bytes.size(); offset += columns) {
        out += std::format("{:04X}: ", static_cast<uint32_t>(offset));
        const size_t rowEnd = std::min(offset + columns, bytes.size());
        for (size_t i = offset; i < rowEnd; ++i) {
            out += std::format("{:02X}", bytes[i]);
            if (i + 1 < rowEnd) {
                out += ' ';
            }
        }
        out += '\n';
    }
    return out;
}

[[nodiscard]] bool consumesDuration(const nspc::NspcEvent& event) {
    return std::holds_alternative<nspc::Note>(event) || std::holds_alternative<nspc::Tie>(event) ||
           std::holds_alternative<nspc::Rest>(event) || std::holds_alternative<nspc::Percussion>(event);
}

[[nodiscard]] std::string formatVcmd(const nspc::Vcmd& cmd,
                                     const std::unordered_map<int, uint16_t>* subroutineAddrById = nullptr) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) { return std::string{"VCMD <empty>"}; },
            [](const nspc::VcmdInst& value) { return std::format("Ins instrument=${:02X}", value.instrumentIndex); },
            [](const nspc::VcmdPanning& value) { return std::format("Pan value=${:02X}", value.panning); },
            [](const nspc::VcmdPanFade& value) {
                return std::format("PFa time=${:02X} target=${:02X}", value.time, value.target);
            },
            [](const nspc::VcmdVibratoOn& value) {
                return std::format("VOn delay=${:02X} rate=${:02X} depth=${:02X}", value.delay, value.rate,
                                   value.depth);
            },
            [](const nspc::VcmdVibratoOff&) { return std::string{"VOf"}; },
            [](const nspc::VcmdGlobalVolume& value) { return std::format("GVl value=${:02X}", value.volume); },
            [](const nspc::VcmdGlobalVolumeFade& value) {
                return std::format("GVF time=${:02X} target=${:02X}", value.time, value.target);
            },
            [](const nspc::VcmdTempo& value) { return std::format("Tmp value=${:02X}", value.tempo); },
            [](const nspc::VcmdTempoFade& value) {
                return std::format("TmF time=${:02X} target=${:02X}", value.time, value.target);
            },
            [](const nspc::VcmdGlobalTranspose& value) { return std::format("GTr semitones={:+d}", value.semitones); },
            [](const nspc::VcmdPerVoiceTranspose& value) {
                return std::format("PTr semitones={:+d}", value.semitones);
            },
            [](const nspc::VcmdTremoloOn& value) {
                return std::format("TOn delay=${:02X} rate=${:02X} depth=${:02X}", value.delay, value.rate,
                                   value.depth);
            },
            [](const nspc::VcmdTremoloOff&) { return std::string{"TOf"}; },
            [](const nspc::VcmdVolume& value) { return std::format("Vol value=${:02X}", value.volume); },
            [](const nspc::VcmdVolumeFade& value) {
                return std::format("VFd time=${:02X} target=${:02X}", value.time, value.target);
            },
            [subroutineAddrById](const nspc::VcmdSubroutineCall& value) {
                uint16_t encodedAddr = value.originalAddr;
                bool resolved = false;
                if (subroutineAddrById != nullptr) {
                    const auto it = subroutineAddrById->find(value.subroutineId);
                    if (it != subroutineAddrById->end()) {
                        encodedAddr = it->second;
                        resolved = true;
                    }
                }

                if (resolved) {
                    return std::format(
                        "Cal subId={} addrRaw=${:04X} addrEnc=${:04X} count=${:02X} iterations={}", value.subroutineId,
                        value.originalAddr, encodedAddr, value.count, static_cast<uint16_t>(value.count));
                }

                return std::format("Cal subId={} addrRaw=${:04X} addrEnc=<unresolved> count=${:02X} iterations={}",
                                   value.subroutineId, value.originalAddr, value.count, static_cast<uint16_t>(value.count));
            },
            [](const nspc::VcmdVibratoFadeIn& value) { return std::format("Vfi time=${:02X}", value.time); },
            [](const nspc::VcmdPitchEnvelopeTo& value) {
                return std::format("PEt delay=${:02X} len=${:02X} semitone=${:02X}", value.delay, value.length,
                                   value.semitone);
            },
            [](const nspc::VcmdPitchEnvelopeFrom& value) {
                return std::format("PEf delay=${:02X} len=${:02X} semitone=${:02X}", value.delay, value.length,
                                   value.semitone);
            },
            [](const nspc::VcmdPitchEnvelopeOff&) { return std::string{"PEo"}; },
            [](const nspc::VcmdFineTune& value) { return std::format("FTn semitones={:+d}", value.semitones); },
            [](const nspc::VcmdEchoOn& value) {
                return std::format("EOn channels=${:02X} left=${:02X} right=${:02X}", value.channels, value.left,
                                   value.right);
            },
            [](const nspc::VcmdEchoOff&) { return std::string{"EOf"}; },
            [](const nspc::VcmdEchoParams& value) {
                return std::format("EPr delay=${:02X} feedback=${:02X} fir=${:02X}", value.delay, value.feedback,
                                   value.firIndex);
            },
            [](const nspc::VcmdEchoVolumeFade& value) {
                return std::format("EVF time=${:02X} left=${:02X} right=${:02X}", value.time, value.leftTarget,
                                   value.rightTarget);
            },
            [](const nspc::VcmdPitchSlideToNote& value) {
                return std::format("PSt delay=${:02X} len=${:02X} note=${:02X}", value.delay, value.length, value.note);
            },
            [](const nspc::VcmdPercussionBaseInstrument& value) {
                return std::format("PIn index=${:02X}", value.index);
            },
            [](const nspc::VcmdNOP& value) { return std::format("NOP bytes=${:04X}", value.nopBytes); },
            [](const nspc::VcmdMuteChannel&) { return std::string{"MCh"}; },
            [](const nspc::VcmdFastForwardOn&) { return std::string{"FFo"}; },
            [](const nspc::VcmdFastForwardOff&) { return std::string{"FFf"}; },
            [](const nspc::VcmdUnused&) { return std::string{"Unu"}; },
            [](const nspc::VcmdExtension& value) {
                std::string text = std::format("Ext FF ${:02X}", value.id);
                for (uint8_t i = 0; i < value.paramCount; ++i) {
                    text += std::format(" ${:02X}", value.params[i]);
                }
                return text;
            },
        },
        cmd.vcmd);
}

[[nodiscard]] std::string formatEvent(const nspc::NspcEvent& event,
                                      const std::unordered_map<int, uint16_t>* subroutineAddrById = nullptr) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) { return std::string{"<empty>"}; },
            [](const nspc::Duration& value) {
                if (value.quantization.has_value() || value.velocity.has_value()) {
                    return std::format("Duration ticks=${:02X} q={} vel={}", value.ticks,
                                       value.quantization.value_or(0), value.velocity.value_or(0));
                }
                return std::format("Duration ticks=${:02X}", value.ticks);
            },
            [subroutineAddrById](const nspc::Vcmd& value) { return formatVcmd(value, subroutineAddrById); },
            [](const nspc::Note& value) { return std::format("Note pitch=${:02X}", value.pitch); },
            [](const nspc::Tie&) { return std::string{"Tie"}; },
            [](const nspc::Rest&) { return std::string{"Rest"}; },
            [](const nspc::Percussion& value) { return std::format("Percussion index=${:02X}", value.index); },
            [](const nspc::Subroutine& value) {
                return std::format("SubroutineMarker id={} addr=${:04X}", value.id, value.originalAddr);
            },
            [](const nspc::End&) { return std::string{"End"}; },
        },
        event);
}

[[nodiscard]] std::string formatSequenceOp(const nspc::NspcSequenceOp& op) {
    return std::visit(
        nspc::overloaded{
            [](const nspc::PlayPattern& value) {
                return std::format("PlayPattern patternId={} addr=${:04X}", value.patternId, value.trackTableAddr);
            },
            [](const nspc::JumpTimes& value) {
                return std::format("JumpTimes count=${:02X} targetIndex={} targetAddr=${:04X}", value.count,
                                   value.target.index.value_or(-1), value.target.addr);
            },
            [](const nspc::AlwaysJump& value) {
                return std::format("AlwaysJump opcode=${:02X} targetIndex={} targetAddr=${:04X}", value.opcode,
                                   value.target.index.value_or(-1), value.target.addr);
            },
            [](const nspc::FastForwardOn&) { return std::string{"FastForwardOn"}; },
            [](const nspc::FastForwardOff&) { return std::string{"FastForwardOff"}; },
            [](const nspc::EndSequence&) { return std::string{"EndSequence"}; },
        },
        op);
}

[[nodiscard]] const nspc::NspcUploadChunk* findChunkByLabel(std::span<const nspc::NspcUploadChunk> chunks,
                                                            std::string_view label) {
    const auto it = std::find_if(chunks.begin(), chunks.end(),
                                 [&](const nspc::NspcUploadChunk& chunk) { return chunk.label == label; });
    if (it == chunks.end()) {
        return nullptr;
    }
    return &(*it);
}

[[nodiscard]] std::optional<size_t> firstMismatch(std::span<const uint8_t> lhs, std::span<const uint8_t> rhs) {
    const size_t commonCount = std::min(lhs.size(), rhs.size());
    for (size_t i = 0; i < commonCount; ++i) {
        if (lhs[i] != rhs[i]) {
            return i;
        }
    }
    if (lhs.size() != rhs.size()) {
        return commonCount;
    }
    return std::nullopt;
}

struct ExpandedCallFrame {
    int subroutineId = -1;
    int iteration = 1;
    int iterationCount = 1;
    nspc::NspcEventId callEventId = 0;
};

struct AnnotatedExpandedTrack {
    std::vector<nspc::NspcEventEntry> events;
    std::vector<std::string> sourceLabels;
    std::vector<std::string> messages;
};

[[nodiscard]] const nspc::VcmdSubroutineCall* asSubroutineCall(const nspc::NspcEventEntry& entry) {
    const auto* vcmd = std::get_if<nspc::Vcmd>(&entry.event);
    if (!vcmd) {
        return nullptr;
    }
    return std::get_if<nspc::VcmdSubroutineCall>(&vcmd->vcmd);
}

[[nodiscard]] bool containsSubroutineId(std::span<const int> stack, int subroutineId) {
    return std::find(stack.begin(), stack.end(), subroutineId) != stack.end();
}

[[nodiscard]] std::string buildSourceLabel(const std::vector<ExpandedCallFrame>& callFrames) {
    if (callFrames.empty()) {
        return "track";
    }

    std::string label;
    for (size_t i = 0; i < callFrames.size(); ++i) {
        if (i > 0) {
            label += " > ";
        }
        const auto& frame = callFrames[i];
        label += std::format("sub{}[{}/{}]@{}", frame.subroutineId, frame.iteration, frame.iterationCount,
                             frame.callEventId);
    }
    return label;
}

AnnotatedExpandedTrack expandTrackWithAnnotations(const nspc::NspcSong& song, const nspc::NspcTrack& track) {
    AnnotatedExpandedTrack out;
    out.events.reserve(track.events.size());
    out.sourceLabels.reserve(track.events.size());

    std::unordered_map<int, const nspc::NspcSubroutine*> subroutineById;
    subroutineById.reserve(song.subroutines().size());
    for (const auto& subroutine : song.subroutines()) {
        subroutineById[subroutine.id] = &subroutine;
    }

    constexpr size_t kMaxInlineDepth = 32;
    std::vector<int> callStack;
    callStack.reserve(kMaxInlineDepth);
    std::vector<ExpandedCallFrame> callFrames;
    callFrames.reserve(kMaxInlineDepth);

    std::function<void(const std::vector<nspc::NspcEventEntry>&, bool)> inlineEvents =
        [&](const std::vector<nspc::NspcEventEntry>& input, bool includeEnd) {
            for (const auto& entry : input) {

                if (std::holds_alternative<nspc::End>(entry.event)) {
                    if (includeEnd) {
                        out.events.push_back(entry);
                        out.sourceLabels.push_back(buildSourceLabel(callFrames));
                    }
                    break;
                }

                const auto* subCall = asSubroutineCall(entry);
                if (!subCall) {
                    out.events.push_back(entry);
                    out.sourceLabels.push_back(buildSourceLabel(callFrames));
                    continue;
                }

                const auto subroutineIt = subroutineById.find(subCall->subroutineId);
                if (subroutineIt == subroutineById.end()) {
                    out.messages.push_back(std::format(
                        "Call event {} references missing subroutine id {}; left unexpanded", entry.id,
                        subCall->subroutineId));
                    out.events.push_back(entry);
                    out.sourceLabels.push_back(buildSourceLabel(callFrames));
                    continue;
                }

                if (callStack.size() >= kMaxInlineDepth || containsSubroutineId(callStack, subCall->subroutineId)) {
                    out.messages.push_back(std::format(
                        "Call event {} to subroutine {} would recurse (depth={}): left unexpanded", entry.id,
                        subCall->subroutineId, callStack.size()));
                    out.events.push_back(entry);
                    out.sourceLabels.push_back(buildSourceLabel(callFrames));
                    continue;
                }

                callStack.push_back(subCall->subroutineId);
                const int iterationCount = static_cast<int>(subCall->count);
                for (int iteration = 0; iteration < iterationCount; ++iteration) {
                    callFrames.push_back(ExpandedCallFrame{
                        .subroutineId = subCall->subroutineId,
                        .iteration = iteration + 1,
                        .iterationCount = iterationCount,
                        .callEventId = entry.id,
                    });
                    inlineEvents(subroutineIt->second->events, false);
                    callFrames.pop_back();
                }
                callStack.pop_back();
            }
        };

    inlineEvents(track.events, true);
    return out;
}

std::string dumpEventStreamDetails(const std::vector<nspc::NspcEventEntry>& events,
                                   const std::unordered_map<int, uint16_t>& subroutineAddrById,
                                   const nspc::NspcEngineConfig& engine,
                                   const nspc::NspcUploadChunk* compiledChunk,
                                   std::span<const std::string> sourceLabels = {}) {
    std::string out;
    std::vector<std::string> warnings;
    const auto encoded = nspc::encodeEventStreamForEngine(events, subroutineAddrById, warnings, engine);

    out += std::format("Event count: {}\n", events.size());
    if (!warnings.empty()) {
        out += std::format("Encoding warnings ({}):\n", warnings.size());
        for (const auto& warning : warnings) {
            out += std::format("  - {}\n", warning);
        }
    }

    if (!encoded.has_value()) {
        out += std::format("Encoding error: {}\n", encoded.error());
        return out;
    }

    out += std::format("Encoded bytes (re-encoded): {}\n", encoded->size());
    if (compiledChunk != nullptr) {
        out += std::format("Encoded bytes (compiled chunk): {}\n", compiledChunk->bytes.size());
        const auto mismatch = firstMismatch(*encoded, compiledChunk->bytes);
        if (!mismatch.has_value()) {
            out += "Encoded bytes match compiled chunk exactly.\n";
        } else {
            out += std::format("Encoded bytes mismatch at offset +{:04X}\n", static_cast<uint32_t>(*mismatch));
        }
    }

    const bool showSourceLabels = sourceLabels.size() == events.size();
    out += "\nEvents:\n";
    if (showSourceLabels) {
        out += "  idx   id                 tick  offs size bytes            source                       event\n";
        out += "  ----  -----------------  ----  ---- ---- ---------------  ---------------------------  --------------------------------------------\n";
    } else {
        out += "  idx   id                 tick  offs size bytes            event\n";
        out += "  ----  -----------------  ----  ---- ---- ---------------  --------------------------------------------\n";
    }

    uint32_t tick = 0;
    nspc::Duration currentDuration{.ticks = 1, .quantization = std::nullopt, .velocity = std::nullopt};
    uint32_t encodedOffset = 0;

    for (size_t i = 0; i < events.size(); ++i) {
        const auto& entry = events[i];
        std::vector<std::string> singleWarnings;
        const std::vector<nspc::NspcEventEntry> oneEvent{entry};
        const auto oneEncoded = nspc::encodeEventStreamForEngine(oneEvent, subroutineAddrById, singleWarnings, engine);

        std::string byteText;
        size_t eventSize = 0;
        if (oneEncoded.has_value()) {
            eventSize = oneEncoded->size();
            for (size_t b = 0; b < oneEncoded->size(); ++b) {
                byteText += std::format("{:02X}", (*oneEncoded)[b]);
                if (b + 1 < oneEncoded->size()) {
                    byteText += ' ';
                }
            }
        } else {
            byteText = std::format("ERR({})", oneEncoded.error());
        }

        if (showSourceLabels) {
            out += std::format("  {:04}  {:017}  {:04X}  {:04X} {:4} {:<15}  {:<27}  {}\n", i, entry.id, tick,
                               encodedOffset, eventSize, byteText, sourceLabels[i],
                               formatEvent(entry.event, &subroutineAddrById));
        } else {
            out += std::format("  {:04}  {:017}  {:04X}  {:04X} {:4} {:<15}  {}\n", i, entry.id, tick, encodedOffset,
                               eventSize, byteText, formatEvent(entry.event, &subroutineAddrById));
        }

        encodedOffset += static_cast<uint32_t>(eventSize);

        if (const auto* duration = std::get_if<nspc::Duration>(&entry.event)) {
            currentDuration = *duration;
            continue;
        }
        if (consumesDuration(entry.event)) {
            tick += currentDuration.ticks;
        }
    }

    out += "\nByte stream:\n";
    if (compiledChunk != nullptr) {
        out += std::format("Chunk label: {}\n", compiledChunk->label);
        out += std::format("Chunk address: ${:04X}\n", compiledChunk->address);
        out += hexDump(compiledChunk->bytes);
    } else {
        out += "(No compiled chunk found)\n";
        out += hexDump(*encoded);
    }

    return out;
}

std::expected<void, std::string> dumpSongOwners(const VariantContext& context, const std::filesystem::path& variantDir) {
    const auto& song = context.song;
    const auto& layout = context.layout;
    const auto& uploadChunks = context.compileOutput.upload.chunks;
    const auto& engine = context.project.engineConfig();

    const auto tracksDir = variantDir / "tracks";
    const auto tracksExpandedDir = variantDir / "tracks_expanded";
    const auto subroutinesDir = variantDir / "subroutines";
    std::error_code mkDirError;
    std::filesystem::create_directories(tracksDir, mkDirError);
    if (mkDirError) {
        return std::unexpected(
            std::format("Failed to create tracks directory '{}': {}", tracksDir.string(), mkDirError.message()));
    }
    std::filesystem::create_directories(tracksExpandedDir, mkDirError);
    if (mkDirError) {
        return std::unexpected(std::format("Failed to create expanded tracks directory '{}': {}",
                                           tracksExpandedDir.string(), mkDirError.message()));
    }
    std::filesystem::create_directories(subroutinesDir, mkDirError);
    if (mkDirError) {
        return std::unexpected(std::format("Failed to create subroutines directory '{}': {}", subroutinesDir.string(),
                                           mkDirError.message()));
    }

    for (const auto& track : song.tracks()) {
        const auto addrIt = layout.trackAddrById.find(track.id);
        const uint16_t address = (addrIt != layout.trackAddrById.end()) ? addrIt->second : 0;
        const auto sizeIt = layout.trackSizeById.find(track.id);
        const uint32_t size = (sizeIt != layout.trackSizeById.end()) ? sizeIt->second : 0;

        std::string fileText;
        fileText += std::format("Track {}\n", track.id);
        fileText += std::format("Original address: ${:04X}\n", track.originalAddr);
        fileText += std::format("Allocated address: ${:04X}\n", address);
        fileText += std::format("Allocated size: {}\n\n", size);

        const std::string chunkLabel = std::format("Track {:02X}", track.id);
        const auto* chunk = findChunkByLabel(uploadChunks, chunkLabel);
        fileText += dumpEventStreamDetails(track.events, layout.subroutineAddrById, engine, chunk);

        const auto outputPath = tracksDir / std::format("track_{}.txt", track.id);
        auto writeResult = writeTextFile(outputPath, fileText);
        if (!writeResult.has_value()) {
            return std::unexpected(writeResult.error());
        }

        std::string expandedFileText;
        expandedFileText += std::format("Track {} (Expanded)\n", track.id);
        expandedFileText += std::format("Original address: ${:04X}\n", track.originalAddr);
        expandedFileText += std::format("Allocated address: ${:04X}\n", address);
        expandedFileText += std::format("Allocated size (compiled track chunk): {}\n\n", size);

        const auto expandedTrack = expandTrackWithAnnotations(song, track);
        if (!expandedTrack.messages.empty()) {
            expandedFileText += std::format("Expansion messages ({}):\n", expandedTrack.messages.size());
            for (const auto& message : expandedTrack.messages) {
                expandedFileText += std::format("  - {}\n", message);
            }
            expandedFileText += "\n";
        }
        expandedFileText += dumpEventStreamDetails(expandedTrack.events, layout.subroutineAddrById, engine, nullptr,
                                                   expandedTrack.sourceLabels);

        const auto expandedOutputPath = tracksExpandedDir / std::format("track_{}.txt", track.id);
        writeResult = writeTextFile(expandedOutputPath, expandedFileText);
        if (!writeResult.has_value()) {
            return std::unexpected(writeResult.error());
        }
    }

    for (const auto& subroutine : song.subroutines()) {
        const auto addrIt = layout.subroutineAddrById.find(subroutine.id);
        const uint16_t address = (addrIt != layout.subroutineAddrById.end()) ? addrIt->second : 0;
        const auto sizeIt = layout.subroutineSizeById.find(subroutine.id);
        const uint32_t size = (sizeIt != layout.subroutineSizeById.end()) ? sizeIt->second : 0;

        std::string fileText;
        fileText += std::format("Subroutine {}\n", subroutine.id);
        fileText += std::format("Original address: ${:04X}\n", subroutine.originalAddr);
        fileText += std::format("Allocated address: ${:04X}\n", address);
        fileText += std::format("Allocated size: {}\n\n", size);

        const std::string chunkLabel = std::format("Subroutine {:02X}", subroutine.id);
        const auto* chunk = findChunkByLabel(uploadChunks, chunkLabel);
        fileText += dumpEventStreamDetails(subroutine.events, layout.subroutineAddrById, engine, chunk);

        const auto outputPath = subroutinesDir / std::format("subroutine_{}.txt", subroutine.id);
        auto writeResult = writeTextFile(outputPath, fileText);
        if (!writeResult.has_value()) {
            return std::unexpected(writeResult.error());
        }
    }

    return {};
}

std::expected<void, std::string> dumpVariantSummary(const VariantContext& context, int songIndex,
                                                    const std::filesystem::path& variantDir,
                                                    const LoadedProjectContext& loadedProject) {
    const auto& song = context.song;
    const auto& layout = context.layout;
    const auto& compileOutput = context.compileOutput;
    const auto& uploadChunks = compileOutput.upload.chunks;

    std::string summary;
    summary += std::format("Variant: {}\n", variantName(context.variant));
    summary += std::format("Song index: {}\n", songIndex);
    summary += std::format("Song id: {}\n", song.songId());
    if (loadedProject.loadedFromOverlay) {
        summary += std::format("Project overlay: {}\n", loadedProject.sourcePath.string());
        summary += std::format("Base SPC: {}\n", loadedProject.sourceSpcPath.string());
    } else {
        summary += std::format("SPC: {}\n", loadedProject.sourceSpcPath.string());
    }
    summary += std::format("Tracks: {}\n", song.tracks().size());
    summary += std::format("Subroutines: {}\n", song.subroutines().size());
    summary += std::format("Patterns: {}\n", song.patterns().size());
    summary += std::format("Sequence ops: {}\n", song.sequence().size());
    summary += "\n";

    if (!compileOutput.warnings.empty()) {
        summary += std::format("Compile warnings ({}):\n", compileOutput.warnings.size());
        for (const auto& warning : compileOutput.warnings) {
            summary += std::format("  - {}\n", warning);
        }
    } else {
        summary += "Compile warnings: none\n";
    }
    summary += "\n";

    summary += "Sequence:\n";
    for (size_t i = 0; i < song.sequence().size(); ++i) {
        summary += std::format("  [{:03}] {}\n", i, formatSequenceOp(song.sequence()[i]));
    }
    summary += "\n";

    summary += "Patterns:\n";
    for (const auto& pattern : song.patterns()) {
        summary += std::format("  Pattern {} originalTable=${:04X} allocatedTable=${:04X}\n", pattern.id,
                               pattern.trackTableAddr,
                               layout.patternAddrById.contains(pattern.id) ? layout.patternAddrById.at(pattern.id) : 0);
        if (pattern.channelTrackIds.has_value()) {
            for (size_t channel = 0; channel < pattern.channelTrackIds->size(); ++channel) {
                summary +=
                    std::format("    ch{} -> track {}\n", channel, (*pattern.channelTrackIds)[channel]);
            }
        } else {
            summary += "    (no channel track map)\n";
        }
    }
    summary += "\n";

    summary += std::format("Address layout sequence=${:04X}\n", layout.sequenceAddr);
    summary += "Track addresses:\n";
    for (const auto& [trackId, address] : layout.trackAddrById) {
        const auto sizeIt = layout.trackSizeById.find(trackId);
        const uint32_t size = (sizeIt != layout.trackSizeById.end()) ? sizeIt->second : 0;
        summary += std::format("  track {} -> ${:04X} ({} bytes)\n", trackId, address, size);
    }
    summary += "Subroutine addresses:\n";
    for (const auto& [subId, address] : layout.subroutineAddrById) {
        const auto sizeIt = layout.subroutineSizeById.find(subId);
        const uint32_t size = (sizeIt != layout.subroutineSizeById.end()) ? sizeIt->second : 0;
        summary += std::format("  sub {} -> ${:04X} ({} bytes)\n", subId, address, size);
    }
    summary += "\n";

    summary += std::format("Upload chunks ({}):\n", uploadChunks.size());
    for (const auto& chunk : uploadChunks) {
        summary += std::format("  ${:04X}  {:5}  {}\n", chunk.address, chunk.bytes.size(), chunk.label);
    }

    auto writeResult = writeTextFile(variantDir / "summary.txt", summary);
    if (!writeResult.has_value()) {
        return std::unexpected(writeResult.error());
    }

    return {};
}

std::expected<VariantContext, std::string> buildVariant(const nspc::NspcProject& sourceProject, int songIndex,
                                                        DumpVariant variant) {
    nspc::NspcProject project = sourceProject;
    auto& songs = project.songs();
    if (songIndex < 0 || songIndex >= static_cast<int>(songs.size())) {
        return std::unexpected(std::format("Song index {} is out of range", songIndex));
    }

    auto& song = songs[static_cast<size_t>(songIndex)];
    if (variant == DumpVariant::Flattened || variant == DumpVariant::FlatOptimized) {
        song.flattenSubroutines();
    }

    nspc::NspcBuildOptions options{};
    options.optimizeSubroutines = (variant == DumpVariant::Optimized || variant == DumpVariant::FlatOptimized);

    auto compileOutput = nspc::buildSongScopedUpload(project, songIndex, options);
    if (!compileOutput.has_value()) {
        return std::unexpected(
            std::format("buildSongScopedUpload failed for variant '{}': {}", variantName(variant), compileOutput.error()));
    }

    nspc::NspcSong dumpSong = song;
    const auto* layout = project.songAddressLayout(dumpSong.songId());
    if (layout == nullptr) {
        return std::unexpected(
            std::format("No address layout produced for song {}", dumpSong.songId()));
    }

    return VariantContext{
        .variant = variant,
        .project = std::move(project),
        .song = std::move(dumpSong),
        .compileOutput = std::move(*compileOutput),
        .layout = *layout,
    };
}

std::expected<void, std::string> dumpVariant(const VariantContext& context, int songIndex, const ToolOptions& options,
                                             const LoadedProjectContext& loadedProject,
                                             std::span<const uint8_t> baseSpcData) {
    const auto variantDir = options.outputDir / variantName(context.variant);
    std::error_code removeError;
    std::filesystem::remove_all(variantDir, removeError);
    if (removeError) {
        return std::unexpected(
            std::format("Failed to clear output directory '{}': {}", variantDir.string(), removeError.message()));
    }
    std::error_code makeError;
    std::filesystem::create_directories(variantDir, makeError);
    if (makeError) {
        return std::unexpected(
            std::format("Failed to create output directory '{}': {}", variantDir.string(), makeError.message()));
    }

    auto summaryResult = dumpVariantSummary(context, songIndex, variantDir, loadedProject);
    if (!summaryResult.has_value()) {
        return std::unexpected(summaryResult.error());
    }

    auto ownersResult = dumpSongOwners(context, variantDir);
    if (!ownersResult.has_value()) {
        return std::unexpected(ownersResult.error());
    }

    if (options.emitSpc) {
        const auto patchedSpc = nspc::applyUploadToSpcImage(context.compileOutput.upload, baseSpcData);
        if (!patchedSpc.has_value()) {
            return std::unexpected(std::format("Failed to build variant SPC '{}': {}", variantName(context.variant),
                                               patchedSpc.error()));
        }

        std::string playbackStateSummary;
        const auto debugPlaybackSpc =
            buildDebugPlaybackSpc(*patchedSpc, context.project.engineConfig(), songIndex, options.triggerPortOverride,
                                  &playbackStateSummary);
        if (!debugPlaybackSpc.has_value()) {
            return std::unexpected(std::format("Failed to prepare debug playback SPC '{}': {}",
                                               variantName(context.variant), debugPlaybackSpc.error()));
        }

        const auto spcOutputPath =
            variantDir / std::format("song_{:02d}_{}.spc", songIndex, variantName(context.variant));
        auto writeSpcResult = writeBinaryFile(spcOutputPath, *debugPlaybackSpc);
        if (!writeSpcResult.has_value()) {
            return std::unexpected(writeSpcResult.error());
        }

        const auto stateOutputPath =
            variantDir / std::format("song_{:02d}_{}.txt", songIndex, variantName(context.variant));
        auto writeStateResult = writeTextFile(stateOutputPath, playbackStateSummary);
        if (!writeStateResult.has_value()) {
            return std::unexpected(writeStateResult.error());
        }
    }

    return {};
}

std::expected<void, std::string> run(const ToolOptions& options) {
    auto loadedProject = loadProject(options);
    if (!loadedProject.has_value()) {
        return std::unexpected(loadedProject.error());
    }

    const auto songCount = loadedProject->project.songs().size();
    if (options.songIndex < 0 || options.songIndex >= static_cast<int>(songCount)) {
        return std::unexpected(
            std::format("Song index {} is out of range (project has {} songs)", options.songIndex, songCount));
    }

    std::error_code createError;
    std::filesystem::create_directories(options.outputDir, createError);
    if (createError) {
        return std::unexpected(
            std::format("Failed to create output directory '{}': {}", options.outputDir.string(), createError.message()));
    }

    std::vector<uint8_t> baseSpcData;
    if (options.emitSpc) {
        auto baseSpc = readBinaryFile(loadedProject->sourceSpcPath);
        if (!baseSpc.has_value()) {
            return std::unexpected(std::format("Failed to read source SPC '{}': {}", loadedProject->sourceSpcPath.string(),
                                               baseSpc.error()));
        }
        baseSpcData = std::move(*baseSpc);
    }

    for (const auto variant : options.variants) {
        auto variantContext = buildVariant(loadedProject->project, options.songIndex, variant);
        if (!variantContext.has_value()) {
            return std::unexpected(variantContext.error());
        }

        auto dumpResult = dumpVariant(*variantContext, options.songIndex, options, *loadedProject, baseSpcData);
        if (!dumpResult.has_value()) {
            return std::unexpected(dumpResult.error());
        }
    }

    std::string indexText;
    indexText += "NTRAK Song Dump\n";
    if (loadedProject->loadedFromOverlay) {
        indexText += std::format("Project overlay: {}\n", loadedProject->sourcePath.string());
        indexText += std::format("Base SPC: {}\n", loadedProject->sourceSpcPath.string());
    } else {
        indexText += std::format("SPC: {}\n", loadedProject->sourceSpcPath.string());
    }
    indexText += std::format("Song index: {}\n", options.songIndex);
    indexText += "\nGenerated variants:\n";
    for (const auto variant : options.variants) {
        indexText += std::format("  - {}\n", variantName(variant));
    }
    indexText += "\nEach variant directory contains:\n";
    indexText += "  summary.txt\n";
    indexText += "  tracks/track_<id>.txt\n";
    indexText += "  tracks_expanded/track_<id>.txt\n";
    indexText += "  subroutines/subroutine_<id>.txt\n";
    if (options.emitSpc) {
        indexText += "  song_<song-index>_<variant>.spc\n";
    }

    auto writeResult = writeTextFile(options.outputDir / "index.txt", indexText);
    if (!writeResult.has_value()) {
        return std::unexpected(writeResult.error());
    }

    return {};
}

}  // namespace
}  // namespace ntrak::tools

int main(int argc, char** argv) {
    auto options = ntrak::tools::parseArgs(argc, argv);
    if (!options.has_value()) {
        std::cerr << "Error: " << options.error() << '\n';
        ntrak::tools::printUsage(std::cerr, argc > 0 ? argv[0] : "ntrak_song_dump");
        return 1;
    }

    auto result = ntrak::tools::run(*options);
    if (!result.has_value()) {
        std::cerr << "Error: " << result.error() << '\n';
        return 1;
    }

    std::cout << std::format("Dump written to '{}'\n", options->outputDir.string());
    return 0;
}
