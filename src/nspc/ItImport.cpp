#include "ntrak/nspc/ItImport.hpp"

#include "ntrak/nspc/BrrCodec.hpp"
#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcConverter.hpp"
#include "ntrak/nspc/NspcEngine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ntrak::nspc {
namespace {

constexpr size_t kItHeaderSize = 0xC0;
constexpr int kMaxNspcChannels = 8;
constexpr int kMaxNspcAssets = 64;
constexpr uint8_t kDefaultItSpeed = 6;
constexpr uint8_t kDefaultItTempo = 125;
constexpr double kItTempoScaleDivisor = 4.8;
constexpr uint8_t kDefaultQvByte = 0x7F;
constexpr int kItNoteOffValue = 254;
constexpr int kItNoteCutValue = 255;

struct ItEnvelopeNode {
    int volume = 0;
    int ticks = 0;
};

struct ItInstrument {
    bool present = true;
    int index = 0;
    std::string name;
    int sampleIndex = 1;  // 1-based IT index
    int fadeOut = 0;
    int globalVolume = 128;
    bool useEnvelope = false;
    bool sustainLoop = false;
    std::vector<ItEnvelopeNode> envelopeNodes;
};

struct ItSample {
    bool present = true;
    int index = 0;
    std::string name;
    std::string fileName;
    bool useLoop = false;
    bool sixteenBit = false;
    bool compressed = false;
    bool stereo = false;
    uint32_t length = 0;
    uint32_t loopBegin = 0;
    uint32_t loopEnd = 0;
    uint32_t c5Speed = 0;
    int globalVolume = 64;
    int defaultVolume = 64;
    std::vector<int16_t> pcm;
};

struct ItCell {
    int note = -1;
    int instrument = -1;
    int volume = -1;
    int command = -1;
    int value = -1;

    [[nodiscard]] bool hasAnyData() const {
        return note >= 0 || instrument >= 0 || volume >= 0 || command >= 0;
    }
};

struct ItPattern {
    int index = 0;
    int rows = 0;
    std::vector<std::vector<ItCell>> channels;  // [channel][row]
};

struct ItModule {
    std::string name;
    std::string message;
    int globalVolume = 128;
    int initialSpeed = kDefaultItSpeed;
    int initialTempo = kDefaultItTempo;
    std::optional<int> loopOrder;

    std::vector<int> initialChannelPanning;
    std::vector<int> initialChannelVolume;
    std::vector<uint8_t> orders;
    std::vector<ItInstrument> instruments;
    std::vector<ItSample> samples;
    std::vector<ItPattern> patterns;
};

class ItReader {
public:
    explicit ItReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool inRange(size_t offset, size_t length = 1) const {
        return offset <= bytes_.size() && length <= (bytes_.size() - offset);
    }

    [[nodiscard]] std::optional<uint8_t> readU8(size_t offset) const {
        if (!inRange(offset, 1)) {
            return std::nullopt;
        }
        return bytes_[offset];
    }

    [[nodiscard]] std::optional<uint16_t> readU16(size_t offset) const {
        if (!inRange(offset, 2)) {
            return std::nullopt;
        }
        return static_cast<uint16_t>(bytes_[offset] | (static_cast<uint16_t>(bytes_[offset + 1]) << 8u));
    }

    [[nodiscard]] std::optional<uint32_t> readU32(size_t offset) const {
        if (!inRange(offset, 4)) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(bytes_[offset]) | (static_cast<uint32_t>(bytes_[offset + 1]) << 8u) |
               (static_cast<uint32_t>(bytes_[offset + 2]) << 16u) | (static_cast<uint32_t>(bytes_[offset + 3]) << 24u);
    }

    [[nodiscard]] std::string readString(size_t offset, size_t length) const {
        if (!inRange(offset, length)) {
            return {};
        }
        std::string value;
        value.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            const uint8_t ch = bytes_[offset + i];
            if (ch == 0) {
                break;
            }
            value.push_back(static_cast<char>(ch));
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.pop_back();
        }
        return value;
    }

    [[nodiscard]] std::span<const uint8_t> slice(size_t offset, size_t length) const {
        if (!inRange(offset, length)) {
            return {};
        }
        return bytes_.subspan(offset, length);
    }

    [[nodiscard]] size_t size() const {
        return bytes_.size();
    }

private:
    std::span<const uint8_t> bytes_;
};

struct WarningCollector {
    std::vector<std::string> warnings;
    std::unordered_set<std::string> seen;

    void add(std::string warning) {
        if (!seen.insert(warning).second) {
            return;
        }
        warnings.push_back(std::move(warning));
    }
};

[[nodiscard]] std::string lowerCopy(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

[[nodiscard]] std::optional<int> parseInteger(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    int value = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value, 10);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] int clampToByte(int value) {
    return std::clamp(value, 0, 0xFF);
}

[[nodiscard]] int clampToDuration(int value) {
    return std::clamp(value, 1, 0x7F);
}

[[nodiscard]] int mapItNoteToNspcPitch(int itNote) {
    const int shifted = itNote - 24;
    return std::clamp(shifted, 0, 0x47);
}

struct DecodedSlide {
    int deltaPerTick = 0;
    int ticksApplied = 0;
    bool fine = false;
};

[[nodiscard]] std::optional<DecodedSlide> decodeItSlide(int slide, int rowTicks) {
    if (slide <= 0) {
        return std::nullopt;
    }

    const int x = (slide >> 4) & 0x0F;
    const int y = slide & 0x0F;
    DecodedSlide decoded{};
    if (x == 0 && y > 0) {
        decoded.deltaPerTick = -y;
        decoded.ticksApplied = std::max(rowTicks - 1, 0);
    } else if (y == 0 && x > 0) {
        decoded.deltaPerTick = x;
        decoded.ticksApplied = std::max(rowTicks - 1, 0);
    } else if (x == 0x0F && y > 0 && y < 0x0F) {
        decoded.deltaPerTick = -y;
        decoded.ticksApplied = 1;
        decoded.fine = true;
    } else if (y == 0x0F && x > 0 && x < 0x0F) {
        decoded.deltaPerTick = x;
        decoded.ticksApplied = 1;
        decoded.fine = true;
    } else {
        return std::nullopt;
    }

    if (decoded.ticksApplied <= 0 || decoded.deltaPerTick == 0) {
        return std::nullopt;
    }
    return decoded;
}

struct DecodedPitchSlide {
    int delta64 = 0;
    bool fineOrExtraFine = false;
};

[[nodiscard]] std::optional<DecodedPitchSlide> decodeItPitchSlide(int slide, int rowTicks) {
    if (slide <= 0) {
        return std::nullopt;
    }

    const bool fine = ((slide & 0xF0) == 0xF0) && ((slide & 0x0F) != 0);
    const bool extraFine = ((slide & 0xF0) == 0xE0) && ((slide & 0x0F) != 0);

    DecodedPitchSlide decoded{};
    decoded.fineOrExtraFine = fine || extraFine;
    if (fine) {
        decoded.delta64 = (slide & 0x0F) * 4;
    } else if (extraFine) {
        decoded.delta64 = (slide & 0x0F);
    } else {
        const int ticks = std::max(rowTicks - 1, 0);
        decoded.delta64 = slide * 4 * ticks;
    }

    if (decoded.delta64 <= 0) {
        return std::nullopt;
    }
    return decoded;
}

[[nodiscard]] uint8_t mapItGlobalVolumeToNspcVolume(int itGlobalVolume) {
    const int clamped = std::clamp(itGlobalVolume, 0, 128);
    // NSPC applies music/global volume in the same squared multiplier domain as per-voice volume,
    // so use a sqrt mapping to preserve IT's linear 0..128 global volume progression.
    const int scaled = clamped * 2;  // IT 0..128 -> linear 0..256 domain
    if (scaled >= 0x100) {
        return 0xFF;
    }
    const int mapped = static_cast<int>(std::lround(std::sqrt(256.0 * static_cast<double>(std::max(scaled, 0)))));
    return static_cast<uint8_t>(clampToByte(mapped));
}

[[nodiscard]] uint8_t mapItTempoToNspcTempo(int itTempo) {
    const int clamped = std::clamp(itTempo, 1, 255);
    const int scaled = static_cast<int>(std::lround(static_cast<double>(clamped) / kItTempoScaleDivisor));
    return static_cast<uint8_t>(std::clamp(scaled, 1, 0xFF));
}

[[nodiscard]] uint8_t mapItPanToNspc(int itPan) {
    const int clamped = std::clamp(itPan, 0, 64);
    const int mapped = static_cast<int>(std::lround(0x14 - (static_cast<double>(clamped) / 3.2)));
    return static_cast<uint8_t>(std::clamp(mapped, 0, 0x14));
}

[[nodiscard]] int resolveSampleIndexForInstrument(const ItModule& module,
                                                  const std::vector<ItInstrument>& sourceInstruments,
                                                  int instrumentIndex) {
    if (module.samples.empty()) {
        return -1;
    }
    if (instrumentIndex < 0 || instrumentIndex >= static_cast<int>(sourceInstruments.size())) {
        return 0;
    }
    const int itSampleIndex = sourceInstruments[static_cast<size_t>(instrumentIndex)].sampleIndex;
    if (itSampleIndex <= 0 || itSampleIndex > static_cast<int>(module.samples.size())) {
        return 0;
    }
    return itSampleIndex - 1;
}

[[nodiscard]] int resolveDefaultNoteVolume(const ItModule& module, const std::vector<ItInstrument>& sourceInstruments,
                                           int instrumentIndex) {
    const int sampleIndex = resolveSampleIndexForInstrument(module, sourceInstruments, instrumentIndex);
    if (sampleIndex >= 0 && sampleIndex < static_cast<int>(module.samples.size())) {
        return std::clamp(module.samples[static_cast<size_t>(sampleIndex)].defaultVolume, 0, 64);
    }
    return 64;
}

[[nodiscard]] bool instrumentNoteOffUsesKeyOff(const std::vector<bool>& noteOffKeyOffByInstrument,
                                               int instrumentIndex) {
    if (instrumentIndex < 0 || instrumentIndex >= static_cast<int>(noteOffKeyOffByInstrument.size())) {
        return false;
    }
    return noteOffKeyOffByInstrument[static_cast<size_t>(instrumentIndex)];
}

[[nodiscard]] uint8_t computeItVoiceVolume(int noteVolume, int channelVolume, const ItModule& module,
                                           const std::vector<ItInstrument>& sourceInstruments, int instrumentIndex) {
    const int clampedNoteVolume = std::clamp(noteVolume, 0, 64);
    const int clampedChannelVolume = std::clamp(channelVolume, 0, 64);

    int instrumentVolume = 128;
    int sampleVolume = 64;
    if (instrumentIndex >= 0 && instrumentIndex < static_cast<int>(sourceInstruments.size())) {
        const ItInstrument& instrument = sourceInstruments[static_cast<size_t>(instrumentIndex)];
        instrumentVolume = std::clamp(instrument.globalVolume, 0, 128);

        const int sampleIndex = resolveSampleIndexForInstrument(module, sourceInstruments, instrumentIndex);
        if (sampleIndex >= 0 && sampleIndex < static_cast<int>(module.samples.size())) {
            sampleVolume = std::clamp(module.samples[static_cast<size_t>(sampleIndex)].globalVolume, 0, 64);
        }
    }

    // IT uses Vol * SV * IV * CV / 262144 for linear channel volume. mITroid maps that to
    // NSPC's 0..255 voice volume with an equivalent sqrt curve over a 0..256 scaled domain.
    const int scaled = (clampedNoteVolume * sampleVolume * instrumentVolume * clampedChannelVolume) / 131072;
    if (scaled >= 0x100) {
        return 0xFF;
    }
    const int mapped = static_cast<int>(std::lround(std::sqrt(256.0 * static_cast<double>(std::max(scaled, 0)))));
    return static_cast<uint8_t>(clampToByte(mapped));
}

[[nodiscard]] int estimateItNoteVolumeFromMapped(uint8_t mappedVolume, int channelVolume, const ItModule& module,
                                                 const std::vector<ItInstrument>& sourceInstruments,
                                                 int instrumentIndex) {
    int bestNoteVolume = 0;
    int bestDiff = 0x7FFFFFFF;
    for (int noteVolume = 0; noteVolume <= 64; ++noteVolume) {
        const uint8_t mapped =
            computeItVoiceVolume(noteVolume, channelVolume, module, sourceInstruments, instrumentIndex);
        const int diff = std::abs(static_cast<int>(mapped) - static_cast<int>(mappedVolume));
        if (diff < bestDiff) {
            bestDiff = diff;
            bestNoteVolume = noteVolume;
        }
    }
    return bestNoteVolume;
}

void pushEvent(std::vector<NspcEventEntry>& events, NspcEventId& nextId, NspcEvent event) {
    events.push_back(NspcEventEntry{
        .id = nextId++,
        .event = std::move(event),
        .originalAddr = std::nullopt,
    });
}

void pushVcmd(std::vector<NspcEventEntry>& events, NspcEventId& nextId, Vcmd command) {
    pushEvent(events, nextId, NspcEvent{command});
}

std::expected<std::string, std::string> buildTrackDedupKey(const NspcTrack& track,
                                                           const NspcEngineConfig& engineConfig) {
    const std::unordered_map<int, uint16_t> noSubroutineAddrs{};
    std::vector<std::string> encodeWarnings;
    auto encoded = encodeEventStreamForEngine(track.events, noSubroutineAddrs, encodeWarnings, engineConfig);
    if (!encoded.has_value()) {
        return std::unexpected(encoded.error());
    }
    return std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size());
}

[[nodiscard]] std::vector<int16_t> makeSilentPcm() {
    return std::vector<int16_t>(16, 0);
}

[[nodiscard]] double clampResampleRatio(double ratio) {
    if (!std::isfinite(ratio) || ratio <= 0.0) {
        return 1.0;
    }
    return std::clamp(ratio, 0.10, 4.00);
}

[[nodiscard]] double findSampleResampleRatio(const ItImportOptions& options, int sampleIndex) {
    for (const auto& entry : options.sampleResampleOptions) {
        if (entry.sampleIndex == sampleIndex) {
            return clampResampleRatio(entry.resampleRatio);
        }
    }
    return 1.0;
}

[[nodiscard]] double effectiveSampleResampleRatio(const ItImportOptions& options, int sampleIndex) {
    return clampResampleRatio(clampResampleRatio(options.globalResampleRatio) * findSampleResampleRatio(options, sampleIndex));
}

[[nodiscard]] std::vector<int16_t> resamplePcmLinear(std::span<const int16_t> source, double ratio) {
    if (source.empty()) {
        return makeSilentPcm();
    }
    const double clampedRatio = clampResampleRatio(ratio);
    const size_t targetCount = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(source.size()) * clampedRatio)));
    if (targetCount == source.size()) {
        return std::vector<int16_t>(source.begin(), source.end());
    }
    if (source.size() == 1) {
        return std::vector<int16_t>(targetCount, source[0]);
    }

    std::vector<int16_t> out(targetCount);
    for (size_t i = 0; i < targetCount; ++i) {
        const double pos = (targetCount <= 1)
                               ? 0.0
                               : (static_cast<double>(i) * static_cast<double>(source.size() - 1)) /
                                     static_cast<double>(targetCount - 1);
        const size_t lo = static_cast<size_t>(std::floor(pos));
        const size_t hi = std::min(lo + 1, source.size() - 1);
        const double t = pos - static_cast<double>(lo);
        const double sample = (1.0 - t) * static_cast<double>(source[lo]) + t * static_cast<double>(source[hi]);
        out[i] = static_cast<int16_t>(std::clamp(static_cast<int>(std::lround(sample)), -32768, 32767));
    }
    return out;
}

[[nodiscard]] double sinc(double x) {
    constexpr double kPi = 3.14159265358979323846;
    if (std::abs(x) < 1e-12) {
        return 1.0;
    }
    const double px = x * kPi;
    return std::sin(px) / px;
}

[[nodiscard]] double lanczosWindow(double x, double a) {
    const double ax = std::abs(x);
    if (ax >= a) {
        return 0.0;
    }
    return sinc(x / a);
}

[[nodiscard]] std::vector<int16_t> resamplePcmLanczos(std::span<const int16_t> source, double ratio) {
    if (source.empty()) {
        return makeSilentPcm();
    }

    const double clampedRatio = clampResampleRatio(ratio);
    const size_t targetCount = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(source.size()) * clampedRatio)));
    if (targetCount == source.size()) {
        return std::vector<int16_t>(source.begin(), source.end());
    }
    if (source.size() == 1) {
        return std::vector<int16_t>(targetCount, source[0]);
    }

    constexpr double kLanczosA = 8.0;
    const double inputPerOutput = static_cast<double>(source.size()) / static_cast<double>(targetCount);
    const double cutoff = std::min(1.0, clampedRatio); // anti-alias when downsampling
    const int taps = static_cast<int>(std::ceil(kLanczosA));

    std::vector<int16_t> out(targetCount);
    for (size_t i = 0; i < targetCount; ++i) {
        const double center = static_cast<double>(i) * inputPerOutput;
        const int base = static_cast<int>(std::floor(center));
        const int from = base - taps + 1;
        const int to = base + taps;

        double acc = 0.0;
        double sum = 0.0;
        for (int n = from; n <= to; ++n) {
            const int idx = std::clamp(n, 0, static_cast<int>(source.size()) - 1);
            const double dx = center - static_cast<double>(n);
            const double weight = cutoff * sinc(cutoff * dx) * lanczosWindow(dx, kLanczosA);
            acc += weight * static_cast<double>(source[static_cast<size_t>(idx)]);
            sum += weight;
        }

        const double sample = (std::abs(sum) > 1e-12) ? (acc / sum) : static_cast<double>(source[static_cast<size_t>(std::clamp(base, 0, static_cast<int>(source.size()) - 1))]);
        out[i] = static_cast<int16_t>(std::clamp(static_cast<int>(std::lround(sample)), -32768, 32767));
    }
    return out;
}

[[nodiscard]] std::vector<int16_t> resamplePcm(std::span<const int16_t> source, double ratio, bool highQuality) {
    if (highQuality) {
        return resamplePcmLanczos(source, ratio);
    }
    return resamplePcmLinear(source, ratio);
}

struct LoopEncodePlan {
    size_t outputSampleCount = 1;
    size_t loopStartSample = 0;
};

[[nodiscard]] LoopEncodePlan buildMitroidLoopEncodePlan(size_t inputSampleCount, size_t loopBeginSample,
                                                        double outputRatio) {
    LoopEncodePlan plan{};
    if (inputSampleCount == 0) {
        return plan;
    }

    const double clampedOutputRatio = clampResampleRatio(outputRatio);
    const double mItroidResampleFactor = 1.0 / clampedOutputRatio;  // mITroid ratio: input / output
    const size_t boundedLoopBegin = std::min(loopBeginSample, inputSampleCount - 1u);
    const double loopSize =
        static_cast<double>(inputSampleCount - boundedLoopBegin) / mItroidResampleFactor;
    const double safeLoopSize = std::max(loopSize, 1.0);
    const double alignedLoopSize = std::ceil(safeLoopSize / 16.0) * 16.0;

    const double baseTarget = static_cast<double>(inputSampleCount) / mItroidResampleFactor;
    const size_t targetSampleCount = static_cast<size_t>(
        std::max<int64_t>(1, std::llround(baseTarget * (alignedLoopSize / safeLoopSize))));
    const size_t alignedLoopSamples = static_cast<size_t>(std::max(16.0, alignedLoopSize));

    plan.outputSampleCount = targetSampleCount;
    plan.loopStartSample = (targetSampleCount > alignedLoopSamples) ? (targetSampleCount - alignedLoopSamples) : 0u;
    return plan;
}

struct ConvertedSampleInfo {
    int sampleIndex = -1;
    std::string name;
    bool looped = false;
    uint32_t sourcePcmSampleCount = 0;
    uint32_t outputPcmSampleCount = 0;
    uint32_t adjustedC5Speed = 0;
    double effectiveResampleRatio = 1.0;
    BrrSample brr;
};

class ItBitReader {
public:
    explicit ItBitReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::optional<uint32_t> readBits(int bits) {
        if (bits <= 0 || bits > 24) {
            return std::nullopt;
        }
        while (bufferedBits_ < bits) {
            if (cursor_ >= bytes_.size()) {
                return std::nullopt;
            }
            bitBuffer_ |= static_cast<uint32_t>(bytes_[cursor_++]) << bufferedBits_;
            bufferedBits_ += 8;
        }
        const uint32_t mask = (1u << bits) - 1u;
        const uint32_t value = bitBuffer_ & mask;
        bitBuffer_ >>= bits;
        bufferedBits_ -= bits;
        return value;
    }

private:
    std::span<const uint8_t> bytes_;
    size_t cursor_ = 0;
    uint32_t bitBuffer_ = 0;
    int bufferedBits_ = 0;
};

struct CompressedDecodeResult {
    std::vector<int> decoded;
    bool truncated = false;
};

template <int DefWidth, int LowerB, int UpperB, int WidthBits>
CompressedDecodeResult decodeItCompressedChannel(const ItReader& reader, size_t& cursor, size_t sampleCount,
                                                 bool is215, int sampleIndex) {
    (void)sampleIndex;
    CompressedDecodeResult result{};
    std::vector<int>& decoded = result.decoded;
    decoded.reserve(sampleCount);

    constexpr size_t kSamplesPerBlock = (DefWidth > 9) ? 0x4000u : 0x8000u;
    while (decoded.size() < sampleCount) {
        if (!reader.inRange(cursor, 2)) {
            result.truncated = true;
            break;
        }
        const uint16_t compressedBlockSize = reader.readU16(cursor).value_or(0);
        cursor += 2;
        if (compressedBlockSize == 0) {
            continue;
        }
        size_t available = 0;
        if (cursor < reader.size()) {
            available = reader.size() - cursor;
        }
        const size_t readableBlockSize = std::min<size_t>(compressedBlockSize, available);
        if (readableBlockSize < static_cast<size_t>(compressedBlockSize)) {
            result.truncated = true;
        }

        const std::span<const uint8_t> block = reader.slice(cursor, readableBlockSize);
        cursor += readableBlockSize;
        if (block.empty()) {
            result.truncated = true;
            break;
        }

        ItBitReader bitReader(block);
        int width = DefWidth;
        uint32_t mem1 = 0;
        uint32_t mem2 = 0;
        auto changeWidth = [](int currentWidth, int code) {
            int nextWidth = code + 1;
            if (nextWidth >= currentWidth) {
                ++nextWidth;
            }
            return nextWidth;
        };
        size_t blockRemaining = std::min(sampleCount - decoded.size(), kSamplesPerBlock);
        while (blockRemaining > 0) {
            if (width > DefWidth) {
                break;
            }
            const auto valueBits = bitReader.readBits(width);
            if (!valueBits.has_value()) {
                result.truncated = true;
                break;
            }
            int value = static_cast<int>(*valueBits);
            const int topBit = 1 << (width - 1);
            bool emit = true;

            if (width <= 6) {
                if (value == topBit) {
                    const auto newWidthBits = bitReader.readBits(WidthBits);
                    if (!newWidthBits.has_value()) {
                        result.truncated = true;
                        break;
                    }
                    width = changeWidth(width, static_cast<int>(*newWidthBits));
                    emit = false;
                }
            } else if (width < DefWidth) {
                const int low = topBit + LowerB;
                const int high = topBit + UpperB;
                if (value >= low && value <= high) {
                    const int widthCode = value - low;
                    width = changeWidth(width, widthCode);
                    emit = false;
                }
            } else {
                if ((value & topBit) != 0) {
                    width = (value & ~topBit) + 1;
                    emit = false;
                } else {
                    value &= ~topBit;
                }
            }

            if (!emit) {
                continue;
            }

            if ((value & topBit) != 0) {
                value -= (topBit << 1);
            }

            mem1 += static_cast<uint32_t>(value);
            mem2 += mem1;
            decoded.push_back(static_cast<int>(is215 ? mem2 : mem1));
            --blockRemaining;
        }
    }

    if (decoded.size() != sampleCount) {
        result.truncated = true;
    }
    return result;
}

[[nodiscard]] int16_t itRawSampleToPcm16(int rawValue, bool sixteenBit, bool signedPcm) {
    if (sixteenBit) {
        const uint16_t raw = static_cast<uint16_t>(rawValue & 0xFFFF);
        if (signedPcm) {
            return static_cast<int16_t>(raw);
        }
        return static_cast<int16_t>(static_cast<int>(raw) - 32768);
    }

    const uint8_t raw = static_cast<uint8_t>(rawValue & 0xFF);
    if (signedPcm) {
        return static_cast<int16_t>(static_cast<int>(static_cast<int8_t>(raw)) * 256);
    }
    return static_cast<int16_t>((static_cast<int>(raw) - 128) * 256);
}

std::expected<ItSample, std::string> parseItSample(const ItReader& reader, uint32_t offset, int sampleIndex,
                                                   WarningCollector& warnings) {
    ItSample sample{};
    sample.present = true;
    sample.index = sampleIndex;

    if (!reader.inRange(offset, 0x50)) {
        return std::unexpected(std::format("IT sample {} header is out of range", sampleIndex + 1));
    }

    sample.fileName = reader.readString(offset + 0x04u, 12);
    sample.globalVolume = reader.readU8(offset + 0x11u).value_or(64);
    const uint8_t flags = reader.readU8(offset + 0x12u).value_or(0);
    sample.useLoop = (flags & 0x10u) != 0;
    sample.sixteenBit = (flags & 0x02u) != 0;
    sample.compressed = (flags & 0x08u) != 0;
    sample.stereo = (flags & 0x04u) != 0;
    sample.defaultVolume = reader.readU8(offset + 0x13u).value_or(64);
    sample.name = reader.readString(offset + 0x14u, 26);
    const uint8_t conversion = reader.readU8(offset + 0x2Eu).value_or(1);
    sample.length = reader.readU32(offset + 0x30u).value_or(0);
    sample.loopBegin = reader.readU32(offset + 0x34u).value_or(0);
    sample.loopEnd = reader.readU32(offset + 0x38u).value_or(0);
    sample.c5Speed = reader.readU32(offset + 0x3Cu).value_or(0);
    const uint32_t samplePtr = reader.readU32(offset + 0x48u).value_or(0);

    if (sample.length == 0 || samplePtr == 0) {
        sample.pcm = makeSilentPcm();
        sample.useLoop = false;
        return sample;
    }

    uint32_t effectiveLength = sample.length;
    if (sample.useLoop && sample.loopEnd > 0) {
        effectiveLength = std::min(sample.loopEnd, sample.length);
    }

    const bool signedPcm = (conversion & 1u) != 0;
    sample.pcm.resize(effectiveLength);

    std::vector<int16_t> leftPcm(effectiveLength, 0);
    std::vector<int16_t> rightPcm;
    if (sample.stereo) {
        rightPcm.assign(effectiveLength, 0);
    }

    size_t cursor = static_cast<size_t>(samplePtr);
    if (sample.compressed) {
        const bool is215 = (conversion & 0x04u) != 0u;
        auto decodeChannelRaw = [&](size_t count) -> CompressedDecodeResult {
            if (sample.sixteenBit) {
                return decodeItCompressedChannel<17, -8, 7, 4>(reader, cursor, count, is215, sampleIndex);
            }
            return decodeItCompressedChannel<9, -4, 3, 3>(reader, cursor, count, is215, sampleIndex);
        };

        const CompressedDecodeResult leftRaw = decodeChannelRaw(effectiveLength);
        if (leftRaw.truncated) {
            warnings.add(std::format(
                "Sample {:02d} '{}' compressed data was truncated; missing frames were zero-filled",
                sampleIndex + 1, sample.name));
        }
        const size_t leftCount = std::min(leftRaw.decoded.size(), static_cast<size_t>(effectiveLength));
        for (size_t i = 0; i < leftCount; ++i) {
            leftPcm[i] = itRawSampleToPcm16(leftRaw.decoded[i], sample.sixteenBit, signedPcm);
        }

        if (sample.stereo) {
            const CompressedDecodeResult rightRaw = decodeChannelRaw(effectiveLength);
            if (rightRaw.truncated) {
                warnings.add(std::format(
                    "Sample {:02d} '{}' compressed right channel was truncated; missing frames were zero-filled",
                    sampleIndex + 1, sample.name));
            }
            const size_t rightCount = std::min(rightRaw.decoded.size(), static_cast<size_t>(effectiveLength));
            for (size_t i = 0; i < rightCount; ++i) {
                rightPcm[i] = itRawSampleToPcm16(rightRaw.decoded[i], sample.sixteenBit, signedPcm);
            }
        }
    } else {
        const size_t bytesPerSample = sample.sixteenBit ? 2u : 1u;
        const size_t channelCount = sample.stereo ? 2u : 1u;
        const size_t byteLength = static_cast<size_t>(effectiveLength) * bytesPerSample * channelCount;
        if (!reader.inRange(samplePtr, byteLength)) {
            return std::unexpected(std::format("IT sample {} PCM data is out of range", sampleIndex + 1));
        }

        auto readChannel = [&](std::vector<int16_t>& outChannel) {
            for (size_t i = 0; i < effectiveLength; ++i) {
                int raw = 0;
                if (sample.sixteenBit) {
                    raw = static_cast<int>(reader.readU16(cursor).value_or(0));
                    cursor += 2;
                } else {
                    raw = static_cast<int>(reader.readU8(cursor).value_or(0));
                    cursor += 1;
                }
                outChannel[i] = itRawSampleToPcm16(raw, sample.sixteenBit, signedPcm);
            }
        };

        readChannel(leftPcm);
        if (sample.stereo) {
            readChannel(rightPcm);
        }
    }

    if (sample.stereo) {
        warnings.add(std::format("Sample {:02d} '{}' is stereo; downmixed to mono", sampleIndex + 1, sample.name));
        for (size_t i = 0; i < effectiveLength; ++i) {
            const int mixed = (static_cast<int>(leftPcm[i]) + static_cast<int>(rightPcm[i])) / 2;
            sample.pcm[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
        }
    } else {
        sample.pcm = std::move(leftPcm);
    }

    if (sample.useLoop && sample.loopBegin >= effectiveLength) {
        warnings.add(std::format("Sample {:02d} '{}' has invalid loop begin {}; loop disabled", sampleIndex + 1,
                                 sample.name, sample.loopBegin));
        sample.useLoop = false;
    }

    if (sample.pcm.empty()) {
        sample.pcm = makeSilentPcm();
        sample.useLoop = false;
    }

    return sample;
}

std::expected<ItInstrument, std::string> parseItInstrument(const ItReader& reader, uint32_t offset, int instrumentIndex) {
    ItInstrument instrument{};
    instrument.present = true;
    instrument.index = instrumentIndex;

    if (!reader.inRange(offset, 0x140)) {
        return std::unexpected(std::format("IT instrument {} header is out of range", instrumentIndex + 1));
    }

    instrument.fadeOut = reader.readU16(offset + 0x14u).value_or(0);
    instrument.globalVolume = reader.readU8(offset + 0x18u).value_or(128);
    instrument.name = reader.readString(offset + 0x20u, 26);
    instrument.sampleIndex = reader.readU8(offset + 0x40u + 121u).value_or(1);

    const uint8_t envFlags = reader.readU8(offset + 0x130u).value_or(0);
    instrument.useEnvelope = (envFlags & 0x01u) != 0;
    instrument.sustainLoop = (envFlags & 0x04u) != 0;
    const int nodeCount = reader.readU8(offset + 0x131u).value_or(0);

    instrument.envelopeNodes.reserve(std::clamp(nodeCount, 0, 25));
    size_t nodeCursor = static_cast<size_t>(offset + 0x136u);
    for (int i = 0; i < nodeCount && i < 25; ++i) {
        if (!reader.inRange(nodeCursor, 3)) {
            break;
        }
        const int volume = reader.readU8(nodeCursor).value_or(0);
        const int ticks = reader.readU16(nodeCursor + 1u).value_or(0);
        instrument.envelopeNodes.push_back(ItEnvelopeNode{
            .volume = volume,
            .ticks = ticks,
        });
        nodeCursor += 3;
    }

    return instrument;
}

std::expected<ItPattern, std::string> parseItPattern(const ItReader& reader, uint32_t offset, int patternIndex) {
    ItPattern pattern{};
    pattern.index = patternIndex;

    if (offset == 0) {
        pattern.rows = 0;
        pattern.channels.assign(64, {});
        return pattern;
    }

    if (!reader.inRange(offset, 8)) {
        return std::unexpected(std::format("IT pattern {} header is out of range", patternIndex));
    }

    const uint16_t packedLength = reader.readU16(offset).value_or(0);
    const uint16_t rows = reader.readU16(offset + 2u).value_or(0);
    pattern.rows = rows;
    pattern.channels.assign(64, std::vector<ItCell>(rows));

    if (rows == 0 || packedLength == 0) {
        return pattern;
    }

    const size_t dataOffset = static_cast<size_t>(offset + 8u);
    if (!reader.inRange(dataOffset, packedLength)) {
        return std::unexpected(std::format("IT pattern {} packed data is out of range", patternIndex));
    }

    std::array<int, 64> lastMask{};
    std::array<int, 64> lastNote{};
    std::array<int, 64> lastInstrument{};
    std::array<int, 64> lastVolume{};
    std::array<int, 64> lastCommand{};
    std::array<int, 64> lastValue{};
    lastMask.fill(0);
    lastNote.fill(-1);
    lastInstrument.fill(-1);
    lastVolume.fill(-1);
    lastCommand.fill(-1);
    lastValue.fill(-1);

    size_t cursor = dataOffset;
    int row = 0;
    const size_t end = dataOffset + packedLength;
    while (row < rows && cursor < end) {
        const uint8_t channelVariable = reader.readU8(cursor).value_or(0);
        ++cursor;

        if (channelVariable == 0) {
            ++row;
            continue;
        }

        const int channel = (channelVariable - 1) & 63;
        int mask = lastMask[channel];
        if ((channelVariable & 0x80u) != 0u) {
            if (cursor >= end) {
                return std::unexpected(std::format("IT pattern {} packed data truncated while reading mask", patternIndex));
            }
            mask = reader.readU8(cursor).value_or(0);
            ++cursor;
            lastMask[channel] = mask;
        }

        if (row < 0 || row >= rows) {
            continue;
        }
        ItCell& cell = pattern.channels[channel][row];

        if ((mask & 0x01) != 0) {
            if (cursor >= end) {
                return std::unexpected(
                    std::format("IT pattern {} packed data truncated while reading note", patternIndex));
            }
            cell.note = reader.readU8(cursor).value_or(0);
            ++cursor;
            lastNote[channel] = cell.note;
        }
        if ((mask & 0x02) != 0) {
            if (cursor >= end) {
                return std::unexpected(
                    std::format("IT pattern {} packed data truncated while reading instrument", patternIndex));
            }
            cell.instrument = reader.readU8(cursor).value_or(0);
            ++cursor;
            lastInstrument[channel] = cell.instrument;
        }
        if ((mask & 0x04) != 0) {
            if (cursor >= end) {
                return std::unexpected(
                    std::format("IT pattern {} packed data truncated while reading volume", patternIndex));
            }
            cell.volume = reader.readU8(cursor).value_or(0);
            ++cursor;
            lastVolume[channel] = cell.volume;
        }
        if ((mask & 0x08) != 0) {
            if (cursor + 1 >= end) {
                return std::unexpected(
                    std::format("IT pattern {} packed data truncated while reading command/value", patternIndex));
            }
            cell.command = reader.readU8(cursor).value_or(0);
            cell.value = reader.readU8(cursor + 1u).value_or(0);
            cursor += 2;
            lastCommand[channel] = cell.command;
            lastValue[channel] = cell.value;
        }

        if ((mask & 0x10) != 0) {
            cell.note = lastNote[channel];
        }
        if ((mask & 0x20) != 0) {
            cell.instrument = lastInstrument[channel];
        }
        if ((mask & 0x40) != 0) {
            cell.volume = lastVolume[channel];
        }
        if ((mask & 0x80) != 0) {
            cell.command = lastCommand[channel];
            cell.value = lastValue[channel];
        }
    }

    return pattern;
}

std::expected<ItModule, std::string> parseItModule(std::span<const uint8_t> bytes, WarningCollector& warnings) {
    ItReader reader(bytes);
    if (!reader.inRange(0, kItHeaderSize)) {
        return std::unexpected("IT file is truncated");
    }

    const auto signature = reader.slice(0, 4);
    if (signature.size() != 4 || signature[0] != 'I' || signature[1] != 'M' || signature[2] != 'P' || signature[3] != 'M') {
        return std::unexpected("File is not a valid Impulse Tracker module (missing IMPM header)");
    }

    ItModule module{};
    module.name = reader.readString(4, 26);

    const uint16_t orderCount = reader.readU16(0x20).value_or(0);
    const uint16_t instrumentCount = reader.readU16(0x22).value_or(0);
    const uint16_t sampleCount = reader.readU16(0x24).value_or(0);
    const uint16_t patternCount = reader.readU16(0x26).value_or(0);

    module.globalVolume = reader.readU8(0x30).value_or(128);
    module.initialSpeed = std::max<int>(1, reader.readU8(0x32).value_or(kDefaultItSpeed));
    module.initialTempo = std::max<int>(1, reader.readU8(0x33).value_or(kDefaultItTempo));

    const uint16_t messageLength = reader.readU16(0x36).value_or(0);
    const uint32_t messageOffset = reader.readU32(0x38).value_or(0);
    if (messageLength > 0 && messageOffset > 0 && reader.inRange(messageOffset, messageLength)) {
        module.message = reader.readString(messageOffset, messageLength);
        module.loopOrder = parseInteger(module.message);
    }

    module.initialChannelPanning.resize(64);
    module.initialChannelVolume.resize(64);
    for (int i = 0; i < 64; ++i) {
        module.initialChannelPanning[static_cast<size_t>(i)] = reader.readU8(0x40u + static_cast<size_t>(i)).value_or(32);
        module.initialChannelVolume[static_cast<size_t>(i)] = reader.readU8(0x80u + static_cast<size_t>(i)).value_or(64);
    }

    size_t cursor = kItHeaderSize;
    if (!reader.inRange(cursor, orderCount)) {
        return std::unexpected("IT order list is truncated");
    }
    module.orders.assign(reader.slice(cursor, orderCount).begin(), reader.slice(cursor, orderCount).end());
    cursor += orderCount;

    std::vector<uint32_t> instrumentOffsets;
    std::vector<uint32_t> sampleOffsets;
    std::vector<uint32_t> patternOffsets;
    instrumentOffsets.reserve(instrumentCount);
    sampleOffsets.reserve(sampleCount);
    patternOffsets.reserve(patternCount);

    for (uint16_t i = 0; i < instrumentCount; ++i) {
        const auto value = reader.readU32(cursor);
        if (!value.has_value()) {
            return std::unexpected("IT instrument offset table is truncated");
        }
        instrumentOffsets.push_back(*value);
        cursor += 4;
    }
    for (uint16_t i = 0; i < sampleCount; ++i) {
        const auto value = reader.readU32(cursor);
        if (!value.has_value()) {
            return std::unexpected("IT sample offset table is truncated");
        }
        sampleOffsets.push_back(*value);
        cursor += 4;
    }
    for (uint16_t i = 0; i < patternCount; ++i) {
        const auto value = reader.readU32(cursor);
        if (!value.has_value()) {
            return std::unexpected("IT pattern offset table is truncated");
        }
        patternOffsets.push_back(*value);
        cursor += 4;
    }

    module.instruments.reserve(instrumentOffsets.size());
    for (size_t i = 0; i < instrumentOffsets.size(); ++i) {
        if (instrumentOffsets[i] == 0) {
            ItInstrument placeholder{};
            placeholder.present = false;
            placeholder.index = static_cast<int>(i);
            placeholder.name = std::format("Instrument {}", i + 1);
            placeholder.sampleIndex = 1;
            placeholder.globalVolume = 128;
            module.instruments.push_back(std::move(placeholder));
            warnings.add(std::format("Instrument {:02d} offset is 0; inserted placeholder to preserve IT indices",
                                     static_cast<int>(i) + 1));
            continue;
        }
        auto instrument = parseItInstrument(reader, instrumentOffsets[i], static_cast<int>(i));
        if (!instrument.has_value()) {
            return std::unexpected(instrument.error());
        }
        module.instruments.push_back(std::move(*instrument));
    }

    module.samples.reserve(sampleOffsets.size());
    for (size_t i = 0; i < sampleOffsets.size(); ++i) {
        if (sampleOffsets[i] == 0) {
            ItSample placeholder{};
            placeholder.present = false;
            placeholder.index = static_cast<int>(i);
            placeholder.name = std::format("Sample {}", i + 1);
            placeholder.length = 0;
            placeholder.useLoop = false;
            placeholder.pcm = makeSilentPcm();
            module.samples.push_back(std::move(placeholder));
            warnings.add(std::format("Sample {:02d} offset is 0; inserted silent placeholder to preserve IT indices",
                                     static_cast<int>(i) + 1));
            continue;
        }
        auto sample = parseItSample(reader, sampleOffsets[i], static_cast<int>(i), warnings);
        if (!sample.has_value()) {
            return std::unexpected(sample.error());
        }
        module.samples.push_back(std::move(*sample));
    }

    module.patterns.reserve(patternOffsets.size());
    for (size_t i = 0; i < patternOffsets.size(); ++i) {
        auto pattern = parseItPattern(reader, patternOffsets[i], static_cast<int>(i));
        if (!pattern.has_value()) {
            return std::unexpected(pattern.error());
        }
        module.patterns.push_back(std::move(*pattern));
    }

    return module;
}

std::expected<std::vector<ConvertedSampleInfo>, std::string>
convertItSamples(const ItModule& module, const ItImportOptions& options, WarningCollector& warnings) {
    const int sampleLimit = std::min<int>(static_cast<int>(module.samples.size()), kMaxNspcAssets);
    if (static_cast<int>(module.samples.size()) > sampleLimit) {
        warnings.add(std::format("IT has {} samples; only first {} are imported", module.samples.size(), sampleLimit));
    }

    std::vector<ConvertedSampleInfo> converted;
    converted.reserve(std::max(sampleLimit, 1));
    for (int i = 0; i < sampleLimit; ++i) {
        const ItSample& itSample = module.samples[static_cast<size_t>(i)];
        const double outputRatio = effectiveSampleResampleRatio(options, i);
        std::vector<int16_t> originalPcm = itSample.pcm.empty() ? makeSilentPcm() : itSample.pcm;
        std::vector<int16_t> pcm;
        if (itSample.useLoop && !originalPcm.empty()) {
            const LoopEncodePlan plan =
                buildMitroidLoopEncodePlan(originalPcm.size(), static_cast<size_t>(itSample.loopBegin), outputRatio);
            const double planRatio =
                static_cast<double>(plan.outputSampleCount) / static_cast<double>(originalPcm.size());
            pcm = resamplePcm(std::span<const int16_t>(originalPcm.data(), originalPcm.size()), planRatio,
                              options.highQualityResampling);
        } else {
            pcm = resamplePcm(std::span<const int16_t>(originalPcm.data(), originalPcm.size()), outputRatio,
                              options.highQualityResampling);
        }

        BrrEncodeOptions encodeOptions{};
        encodeOptions.enableLoop = itSample.useLoop && !pcm.empty();
        encodeOptions.enhanceTreble = options.enhanceTrebleOnEncode;
        if (encodeOptions.enableLoop) {
            const LoopEncodePlan plan =
                buildMitroidLoopEncodePlan(originalPcm.size(), static_cast<size_t>(itSample.loopBegin), outputRatio);
            encodeOptions.loopStartSample = std::min(plan.loopStartSample, pcm.size() - 1u);
        }

        auto encoded = encodePcm16ToBrr(std::span<const int16_t>(pcm.data(), pcm.size()), encodeOptions);
        if (!encoded.has_value()) {
            return std::unexpected(
                std::format("Failed to BRR-encode IT sample {} '{}': {}", i + 1, itSample.name, encoded.error()));
        }

        const double rateRatio = (originalPcm.empty())
                                     ? 1.0
                                     : (static_cast<double>(pcm.size()) / static_cast<double>(originalPcm.size()));
        const uint32_t adjustedC5 =
            static_cast<uint32_t>(std::max<int64_t>(0, std::llround(static_cast<double>(itSample.c5Speed) * rateRatio)));

        ConvertedSampleInfo entry{};
        entry.sampleIndex = i;
        entry.name = itSample.name;
        entry.looped = encodeOptions.enableLoop;
        entry.sourcePcmSampleCount = static_cast<uint32_t>(originalPcm.size());
        entry.outputPcmSampleCount = static_cast<uint32_t>(pcm.size());
        entry.adjustedC5Speed = adjustedC5;
        entry.effectiveResampleRatio = rateRatio;
        entry.brr = BrrSample{
            .id = i,
            .name = itSample.name,
            .data = std::move(encoded->bytes),
            .originalAddr = 1,  // Non-zero sentinel to preserve loop offset in portSong.
            .originalLoopAddr = static_cast<uint16_t>(1u + encoded->loopOffsetBytes),
            .contentOrigin = NspcContentOrigin::UserProvided,
        };
        converted.push_back(std::move(entry));
    }

    if (converted.empty()) {
        ConvertedSampleInfo silent{};
        silent.sampleIndex = 0;
        silent.name = "Silent";
        silent.sourcePcmSampleCount = 16;
        silent.outputPcmSampleCount = 16;
        silent.adjustedC5Speed = 0;
        silent.effectiveResampleRatio = 1.0;
        silent.brr = BrrSample{
            .id = 0,
            .name = "Silent",
            .data = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
            .originalAddr = 1,
            .originalLoopAddr = 1,
            .contentOrigin = NspcContentOrigin::UserProvided,
        };
        converted.push_back(std::move(silent));
        warnings.add("IT had no decodable samples; inserted a silent placeholder sample");
    }

    return converted;
}

std::vector<ItInstrument> materializeItInstruments(const ItModule& module, size_t convertedSampleCount,
                                                   WarningCollector& warnings) {
    std::vector<ItInstrument> instruments = module.instruments;
    if (!instruments.empty()) {
        return instruments;
    }

    instruments.reserve(convertedSampleCount);
    for (size_t i = 0; i < convertedSampleCount; ++i) {
        instruments.push_back(ItInstrument{
            .index = static_cast<int>(i),
            .name = std::format("Sample {}", i + 1),
            .sampleIndex = static_cast<int>(i) + 1,
            .fadeOut = 0,
            .globalVolume = 128,
            .useEnvelope = false,
            .sustainLoop = false,
            .envelopeNodes = {},
        });
    }
    warnings.add("IT file has no instruments; generated one instrument per imported sample");
    return instruments;
}

std::vector<int> collectOrderedPatternReferences(const ItModule& module, WarningCollector& warnings) {
    std::vector<int> orderPatterns;
    orderPatterns.reserve(module.orders.size());
    for (size_t i = 0; i < module.orders.size(); ++i) {
        const uint8_t order = module.orders[i];
        if (order == 0xFF) {
            break;
        }
        if (order == 0xFE) {
            // IT order separator ("---"): a structural marker, not a playable pattern.
            continue;
        }
        if (order >= module.patterns.size()) {
            warnings.add(std::format("Order {:02d} references missing pattern {:02X}; skipped", static_cast<int>(i),
                                     static_cast<int>(order)));
            continue;
        }
        orderPatterns.push_back(static_cast<int>(order));
    }
    if (orderPatterns.empty() && !module.patterns.empty()) {
        orderPatterns.push_back(0);
        warnings.add("IT order list had no playable patterns; imported pattern 00 as fallback");
    }
    return orderPatterns;
}

struct ExtensionSupport {
    std::optional<uint8_t> legatoVcmd;
    std::optional<uint8_t> arpeggioVcmd;
    std::optional<uint8_t> noPatternKoffVcmd;
    std::vector<std::string> enabledNames;
};

const NspcEngineExtension* findExtensionByContains(const NspcEngineConfig& config, std::string_view needle) {
    const std::string loweredNeedle = lowerCopy(needle);
    for (const auto& extension : config.extensions) {
        const std::string loweredName = lowerCopy(extension.name);
        if (loweredName.find(loweredNeedle) != std::string::npos) {
            return &extension;
        }
    }
    return nullptr;
}

std::optional<uint8_t> findExtensionVcmdId(const NspcEngineExtension& extension, std::string_view vcmdNameNeedle) {
    const std::string loweredNeedle = lowerCopy(vcmdNameNeedle);
    for (const auto& vcmd : extension.vcmds) {
        const std::string loweredName = lowerCopy(vcmd.name);
        if (loweredName.find(loweredNeedle) != std::string::npos) {
            return vcmd.id;
        }
    }
    if (!extension.vcmds.empty()) {
        return extension.vcmds.front().id;
    }
    return std::nullopt;
}

ExtensionSupport enableImportExtensions(NspcProject& project, WarningCollector& warnings) {
    ExtensionSupport support{};

    const NspcEngineExtension* legato = findExtensionByContains(project.engineConfig(), "legato");
    if (legato != nullptr) {
        if (!project.isEngineExtensionEnabled(legato->name)) {
            if (project.setEngineExtensionEnabled(legato->name, true)) {
                support.enabledNames.push_back(legato->name);
            }
        }
        support.legatoVcmd = findExtensionVcmdId(*legato, "legato");
    } else {
        warnings.add("Legato extension not available in engine config; IT legato behavior falls back to regular notes");
    }

    const NspcEngineExtension* arpeggio = findExtensionByContains(project.engineConfig(), "arpeggio");
    if (arpeggio != nullptr) {
        if (!project.isEngineExtensionEnabled(arpeggio->name)) {
            if (project.setEngineExtensionEnabled(arpeggio->name, true)) {
                support.enabledNames.push_back(arpeggio->name);
            }
        }
        support.arpeggioVcmd = findExtensionVcmdId(*arpeggio, "arpeggio");
    } else {
        warnings.add("Arpeggio extension not available in engine config; IT arpeggio effect is ignored");
    }

    const NspcEngineExtension* noPatternKoff = findExtensionByContains(project.engineConfig(), "koff");
    if (noPatternKoff != nullptr) {
        if (!project.isEngineExtensionEnabled(noPatternKoff->name)) {
            if (project.setEngineExtensionEnabled(noPatternKoff->name, true)) {
                support.enabledNames.push_back(noPatternKoff->name);
            }
        }
        support.noPatternKoffVcmd = findExtensionVcmdId(*noPatternKoff, "koff");
    } else {
        warnings.add("No Pattern KOFF extension not available in engine config; pattern-end key-off behavior is unchanged");
    }

    return support;
}

int findClosestMitroidTable(std::span<const int> table, double value, double multiplier) {
    double diff = std::numeric_limits<double>::max();
    for (size_t i = 0; i < table.size(); ++i) {
        const double newDiff = std::abs((static_cast<double>(table[i]) * multiplier) - value);
        if (newDiff < diff) {
            diff = newDiff;
            continue;
        }
        if (i == 0) {
            return 0;
        }
        return static_cast<int>(i - 1);
    }
    return static_cast<int>(table.size()) - 1;
}

void convertInstrumentAdsr(const ItInstrument& source, int initialItTempo, NspcInstrument& target) {
    if (!source.useEnvelope || source.envelopeNodes.size() < 4) {
        target.adsr1 = 0x00;
        target.adsr2 = 0x00;
        target.gain = 0x7F;
        return;
    }

    int aDuration = source.envelopeNodes[1].ticks - source.envelopeNodes[0].ticks;
    int dDuration = source.envelopeNodes[2].ticks - source.envelopeNodes[1].ticks;
    int sDuration = source.envelopeNodes[3].ticks - source.envelopeNodes[2].ticks;
    int sVolume = static_cast<int>(std::lround(static_cast<double>(source.envelopeNodes[2].volume) / 8.0)) - 1;

    constexpr std::array<int, 16> kAttackTable = {
        4100, 2500, 1500, 1000, 640, 380, 260, 160, 96, 64, 40, 24, 16, 10, 6, 0,
    };
    constexpr std::array<int, 8> kDecayTable = {
        1200, 740, 440, 290, 180, 110, 74, 37,
    };
    constexpr std::array<int, 32> kSustainTable = {
        65535, 38000, 28000, 24000, 19000, 14000, 12000, 9400, 7100, 5900, 4700, 3500, 2900, 2400, 1800, 1500,
        1200, 880, 740, 590, 440, 370, 290, 220, 180, 150, 110, 92, 74, 55, 37, 18,
    };
    constexpr std::array<double, 8> kDecayMult = {
        0.724, 0.518, 0.378, 0.263, 0.181, 0.110, 0.052, 0.0,
    };
    constexpr std::array<double, 8> kSustainMult = {
        0.407, 0.623, 0.768, 0.876, 0.962, 1.032, 1.095, 1.149,
    };

    int attack = 0;
    int decay = 0;
    int sustainRate = 0;
    sVolume = std::clamp(sVolume, 0, 0x07);

    // mITroid compatibility: default to the "new/time-based" ADSR mapping.
    const int moduleInitialTempo = std::max(1, static_cast<int>(std::lround(static_cast<double>(initialItTempo) / 4.85)));
    const double itTempo = static_cast<double>(moduleInitialTempo) * 4.8;
    if (itTempo > 0.0) {
        const double millisPerTick = 2500.0 / itTempo;
        if (aDuration == 1) {
            attack = 0x0F;
        } else {
            attack = findClosestMitroidTable(kAttackTable, static_cast<double>(aDuration) * millisPerTick, 1.0);
        }

        const int origDecayTicks = dDuration;
        const int clampedSVol = std::clamp(sVolume, 0, 0x07);
        decay = findClosestMitroidTable(kDecayTable, static_cast<double>(dDuration) * millisPerTick,
                                        kDecayMult[static_cast<size_t>(clampedSVol)]);
        const int actualDecayTicks = static_cast<int>(
            std::lround((static_cast<double>(kDecayTable[static_cast<size_t>(decay)]) *
                         kDecayMult[static_cast<size_t>(clampedSVol)]) /
                        millisPerTick));

        sDuration += (origDecayTicks - actualDecayTicks);
        if (source.sustainLoop) {
            sustainRate = 0x00;
        } else {
            sustainRate =
                findClosestMitroidTable(kSustainTable, static_cast<double>(sDuration) * millisPerTick,
                                        kSustainMult[static_cast<size_t>(clampedSVol)]);
        }
    } else {
        // Fallback to old/tick-based mapping.
        aDuration = std::max(aDuration + 1, 1);
        dDuration = std::max(dDuration + 1, 1);
        sDuration = std::max(sDuration + 1, 1);
        attack = 0x0F - std::min(0x0F, aDuration);
        decay = 0x07 - (std::min(0x0F, dDuration) >> 1);
        sustainRate = 0x1F - std::min(0x1F, sDuration << 1);
    }

    target.adsr1 = static_cast<uint8_t>(0x80 | ((decay & 0x07) << 4) | (attack & 0x0F));
    target.adsr2 = static_cast<uint8_t>(((sVolume & 0x07) << 5) | (sustainRate & 0x1F));
    target.gain = 0x00;
}

std::pair<uint8_t, uint8_t> convertC5SpeedPitch(uint32_t c5Speed) {
    if (c5Speed == 0) {
        return {0x00, 0x00};
    }
    constexpr uint32_t kC5Divisor = 4186;
    const uint32_t mult = c5Speed / kC5Divisor;
    const uint32_t mod = c5Speed % kC5Divisor;
    const int frac = static_cast<int>(std::lround(255.0 * (static_cast<double>(mod) / static_cast<double>(kC5Divisor))));
    return {static_cast<uint8_t>(clampToByte(static_cast<int>(mult))), static_cast<uint8_t>(clampToByte(frac))};
}

struct RowTiming {
    std::vector<uint8_t> ticks;
    int breakRow = 0;
    int endingSpeed = kDefaultItSpeed;
};

RowTiming computeRowTiming(const ItPattern& pattern, int initialSpeed) {
    RowTiming timing{};
    const int clampedInitialSpeed = std::max(initialSpeed, 1);
    timing.endingSpeed = clampedInitialSpeed;
    timing.breakRow = pattern.rows;
    if (pattern.rows <= 0) {
        return timing;
    }

    int breakRow = pattern.rows;
    for (int channel = 0; channel < static_cast<int>(pattern.channels.size()); ++channel) {
        const auto& cells = pattern.channels[static_cast<size_t>(channel)];
        for (int row = 0; row < pattern.rows && row < static_cast<int>(cells.size()); ++row) {
            const ItCell& cell = cells[static_cast<size_t>(row)];
            if (cell.command < 0) {
                continue;
            }
            const int letter = cell.command + 64;
            if (letter == 'C') {
                breakRow = std::min(breakRow, row + 1);
            }
        }
    }
    breakRow = std::max(breakRow, 0);
    timing.breakRow = breakRow;

    int speed = clampedInitialSpeed;
    timing.ticks.resize(static_cast<size_t>(breakRow), static_cast<uint8_t>(clampToDuration(speed)));
    for (int row = 0; row < breakRow; ++row) {
        for (int channel = 0; channel < static_cast<int>(pattern.channels.size()); ++channel) {
            const auto& cells = pattern.channels[static_cast<size_t>(channel)];
            if (row >= static_cast<int>(cells.size())) {
                continue;
            }
            const ItCell& cell = cells[static_cast<size_t>(row)];
            if (cell.command < 0) {
                continue;
            }
            const int letter = cell.command + 64;
            if (letter == 'A') {
                if (cell.value > 0) {
                    speed = cell.value;
                }
            }
        }
        timing.ticks[static_cast<size_t>(row)] = static_cast<uint8_t>(clampToDuration(speed));
    }
    timing.endingSpeed = speed;

    return timing;
}

std::optional<int> findFirstRowTempo(const ItPattern& pattern, int row) {
    for (int channel = 0; channel < static_cast<int>(pattern.channels.size()); ++channel) {
        const auto& cells = pattern.channels[static_cast<size_t>(channel)];
        if (row >= static_cast<int>(cells.size())) {
            continue;
        }
        const ItCell& cell = cells[static_cast<size_t>(row)];
        if (cell.command < 0 || cell.value <= 0) {
            continue;
        }
        if (cell.command + 64 == 'T') {
            return cell.value;
        }
    }
    return std::nullopt;
}

struct TrackState {
    int currentVolume = 0xFF;
    int currentInstrument = -1;
    int currentNoteVolume = 64;
    int currentChannelVolume = 64;
    int lastGlobalVolume = 128;
    int lastGlobalVolumeSlide = 0;
    int lastTempo = kDefaultItTempo;
    int lastTempoSlide = 0;
    int lastVolumeSlide = 0;
    int lastPortamentoDown = 0;
    int lastPortamentoUp = 0;
    int lastVibratoRate = 0;
    int lastVibratoDepth = 0;
    int lastTremoloRate = 0;
    int lastTremoloDepth = 0;
    int activeVibratoRate = 0;
    int activeVibratoDepth = 0;
    int activeTremoloRate = 0;
    int activeTremoloDepth = 0;
    int lastPortamento = 0;
    int lastArpeggio = 0;
    int lastPlayedPitch = -1;
    int pitchSlideRemainder64 = 0;
    bool forceRetriggerNextNoteAfterEfSlide = false;
    bool vibratoEnabled = false;
    bool tremoloEnabled = false;
    bool arpeggioEnabled = false;
    int activeArpeggio = 0;
    bool pendingBoundaryVibratoOff = false;
    bool pendingBoundaryTremoloOff = false;
    bool pendingBoundaryArpeggioOff = false;
    bool hasMeaningfulData = false;
};

struct EffectApplyResult {
    bool consumeCurrentRowNoteAsPortamentoTarget = false;
    bool emittedPitchEnvelope = false;
    bool usedVibrato = false;
    bool usedTremolo = false;
    bool usedArpeggio = false;
    int noteCutTicks = -1;
    int noteDelayTicks = -1;
    std::optional<int> resultingPitchAfterRow = std::nullopt;
};

struct TrackConversionResult {
    NspcTrack track;
    TrackState endState;
};

struct VolumeColumnMappedEffect {
    int letter = -1;
    int value = 0;
    bool useLastVibratoRate = false;
};

[[nodiscard]] std::optional<VolumeColumnMappedEffect> mapItVolumeColumnEffect(int volumeColumn) {
    if (volumeColumn < 65 || volumeColumn > 212) {
        return std::nullopt;
    }

    if (volumeColumn <= 104) {
        // a0x/b0x/c0x/d0x -> Dxy equivalents.
        if (volumeColumn <= 74) {
            return VolumeColumnMappedEffect{.letter = 'D', .value = ((volumeColumn - 65) << 4) | 0x0F};
        }
        if (volumeColumn <= 84) {
            return VolumeColumnMappedEffect{.letter = 'D', .value = 0xF0 | (volumeColumn - 75)};
        }
        if (volumeColumn <= 94) {
            return VolumeColumnMappedEffect{.letter = 'D', .value = (volumeColumn - 85) << 4};
        }
        return VolumeColumnMappedEffect{.letter = 'D', .value = volumeColumn - 95};
    }

    if (volumeColumn <= 124) {
        // e0x/f0x -> E/F with 4x coarser parameter scale.
        if (volumeColumn <= 114) {
            return VolumeColumnMappedEffect{.letter = 'E', .value = (volumeColumn - 105) * 4};
        }
        return VolumeColumnMappedEffect{.letter = 'F', .value = (volumeColumn - 115) * 4};
    }

    if (volumeColumn <= 202) {
        // g0x translation table.
        static constexpr std::array<int, 10> kGxxFromVolumeColumn = {0x00, 0x01, 0x04, 0x08, 0x10,
                                                                      0x20, 0x40, 0x60, 0x80, 0xFF};
        return VolumeColumnMappedEffect{
            .letter = 'G',
            .value = kGxxFromVolumeColumn[static_cast<size_t>(volumeColumn - 193)],
        };
    }

    // h0x vibrato depth: use prior vibrato speed memory from H/U effects.
    return VolumeColumnMappedEffect{
        .letter = 'H',
        .value = volumeColumn - 203,
        .useLastVibratoRate = true,
    };
}

void mergeEffectApplyResult(EffectApplyResult& merged, const EffectApplyResult& update) {
    merged.consumeCurrentRowNoteAsPortamentoTarget =
        merged.consumeCurrentRowNoteAsPortamentoTarget || update.consumeCurrentRowNoteAsPortamentoTarget;
    merged.emittedPitchEnvelope = merged.emittedPitchEnvelope || update.emittedPitchEnvelope;
    merged.usedVibrato = merged.usedVibrato || update.usedVibrato;
    merged.usedTremolo = merged.usedTremolo || update.usedTremolo;
    merged.usedArpeggio = merged.usedArpeggio || update.usedArpeggio;
    if (update.noteCutTicks >= 0) {
        merged.noteCutTicks = update.noteCutTicks;
    }
    if (update.noteDelayTicks >= 0) {
        merged.noteDelayTicks = update.noteDelayTicks;
    }
    if (update.resultingPitchAfterRow.has_value()) {
        merged.resultingPitchAfterRow = update.resultingPitchAfterRow;
    }
}

EffectApplyResult applyItEffect(const ItCell& cell, int mappedNotePitch, std::vector<NspcEventEntry>& events,
                                NspcEventId& nextEventId, TrackState& state,
                                const ExtensionSupport& extensionSupport, const ItModule& module,
                                const std::vector<ItInstrument>& sourceInstruments, WarningCollector& warnings,
                                int patternIndex, int channel, int row, int rowTicks) {
    EffectApplyResult result{};
    if (cell.command < 0) {
        return result;
    }

    const int letter = cell.command + 64;
    const int value = std::max(cell.value, 0);
    auto rowLabel = std::format("pattern {:02X} ch {} row {}", patternIndex, channel, row);
    auto emitSlideToNote = [&](int slideRate, bool consumeNoteTarget) {
        if (mappedNotePitch < 0 || state.lastPlayedPitch < 0) {
            return;
        }
        const int clampedRate = std::clamp(slideRate, 1, 0xFF);
        const int semitoneDistance = std::abs(mappedNotePitch - state.lastPlayedPitch);
        const int slideTicks = (semitoneDistance <= 0) ? 1 : ((semitoneDistance * 16 + clampedRate - 1) / clampedRate);
        const uint8_t normalizedLength = static_cast<uint8_t>(std::clamp(slideTicks, 1, 0xFF));
        pushVcmd(events, nextEventId, Vcmd{VcmdPitchSlideToNote{
                                          .delay = 1,
                                          .length = normalizedLength,
                                          .note = static_cast<uint8_t>(mappedNotePitch),
                                      }});
        result.consumeCurrentRowNoteAsPortamentoTarget = consumeNoteTarget;
        state.hasMeaningfulData = true;
    };
    auto emitSlideToAbsoluteNote = [&](int sourcePitch, int targetPitch, int delayTicks, int lengthTicks) {
        if (sourcePitch < 0) {
            return;
        }
        const uint8_t clampedTarget = static_cast<uint8_t>(std::clamp(targetPitch, 0, 0x47));
        const uint8_t clampedDelay = static_cast<uint8_t>(std::clamp(delayTicks, 0, 0xFF));
        const uint8_t clampedLength = static_cast<uint8_t>(std::clamp(lengthTicks, 1, 0xFF));
        pushVcmd(events, nextEventId, Vcmd{VcmdPitchSlideToNote{
                                          .delay = clampedDelay,
                                          .length = clampedLength,
                                          .note = clampedTarget,
                                      }});
        result.resultingPitchAfterRow = static_cast<int>(clampedTarget);
        state.hasMeaningfulData = true;
    };
    auto emitVibrato = [&](int packedValue) {
        int rate = packedValue >> 4;
        int depth = packedValue & 0x0F;
        if (packedValue == 0) {
            rate = state.lastVibratoRate;
            depth = state.lastVibratoDepth;
        } else {
            state.lastVibratoRate = rate;
            state.lastVibratoDepth = depth;
        }
        if (rate == 0 && depth == 0) {
            return;
        }
        result.usedVibrato = true;
        if (state.vibratoEnabled && state.activeVibratoRate == rate && state.activeVibratoDepth == depth) {
            return;
        }
        pushVcmd(events, nextEventId, Vcmd{VcmdVibratoOn{
                                          .delay = 0,
                                          .rate = static_cast<uint8_t>(clampToByte(rate * 4)),
                                          .depth = static_cast<uint8_t>(clampToByte(depth * 16)),
                                      }});
        state.activeVibratoRate = rate;
        state.activeVibratoDepth = depth;
        state.vibratoEnabled = true;
        state.hasMeaningfulData = true;
    };
    auto emitTremolo = [&](int packedValue) {
        int rate = packedValue >> 4;
        int depth = packedValue & 0x0F;
        if (packedValue == 0) {
            rate = state.lastTremoloRate;
            depth = state.lastTremoloDepth;
        } else {
            state.lastTremoloRate = rate;
            state.lastTremoloDepth = depth;
        }
        if (rate == 0 && depth == 0) {
            return;
        }
        result.usedTremolo = true;
        if (state.tremoloEnabled && state.activeTremoloRate == rate && state.activeTremoloDepth == depth) {
            return;
        }
        pushVcmd(events, nextEventId, Vcmd{VcmdTremoloOn{
                                          .delay = 0,
                                          .rate = static_cast<uint8_t>(clampToByte(rate * 4)),
                                          .depth = static_cast<uint8_t>(clampToByte(depth * 16)),
                                      }});
        state.activeTremoloRate = rate;
        state.activeTremoloDepth = depth;
        state.tremoloEnabled = true;
        state.hasMeaningfulData = true;
    };
    auto applyVolumeSlide = [&](int slide) {
        const auto decoded = decodeItSlide(slide, rowTicks);
        if (!decoded.has_value()) {
            return;
        }
        const int mappedDelta = decoded->deltaPerTick * decoded->ticksApplied;
        const uint8_t mappedVolume = static_cast<uint8_t>(std::clamp(static_cast<int>(state.currentVolume) + mappedDelta,
                                                                      0, 0xFF));
        state.currentVolume = mappedVolume;
        state.currentNoteVolume =
            estimateItNoteVolumeFromMapped(mappedVolume, state.currentChannelVolume, module, sourceInstruments,
                                           state.currentInstrument);
        if (decoded->ticksApplied > 1 && !decoded->fine) {
            // IT Dxy (non-fine) updates from tick 1..N-1 on the row, but using a full-row NSPC
            // fade duration avoids a 1-tick gap between chained row fades while preserving end target.
            const int fadeTime = std::clamp(rowTicks, 1, 0xFF);
            pushVcmd(events, nextEventId, Vcmd{VcmdVolumeFade{
                                              .time = static_cast<uint8_t>(std::clamp(static_cast<int>(fadeTime), 1, 0xFF)),
                                              .target = mappedVolume,
                                          }});
        } else {
            pushVcmd(events, nextEventId, Vcmd{VcmdVolume{mappedVolume}});
        }
        state.hasMeaningfulData = true;
    };

    switch (letter) {
    case 'T': {
        int itTempo = state.lastTempo;
        if (value >= 0x20) {
            itTempo = value;
            state.lastTempo = itTempo;
            state.lastTempoSlide = 0;
        } else {
            int slidePerTick = 0;
            if (value == 0) {
                slidePerTick = state.lastTempoSlide;
            } else if ((value & 0xF0) == 0x10) {
                slidePerTick = value & 0x0F;
                state.lastTempoSlide = slidePerTick;
            } else if ((value & 0xF0) == 0x00) {
                slidePerTick = -(value & 0x0F);
                state.lastTempoSlide = slidePerTick;
            }
            if (slidePerTick == 0) {
                break;
            }
            const int ticks = std::max(rowTicks - 1, 0);
            itTempo = std::clamp(state.lastTempo + (slidePerTick * ticks), 1, 255);
            state.lastTempo = itTempo;
        }
        const uint8_t tempo = mapItTempoToNspcTempo(itTempo);
        pushVcmd(events, nextEventId, Vcmd{VcmdTempo{tempo}});
        state.hasMeaningfulData = true;
        break;
    }
    case 'V': {
        if (value > 0x80) {
            break;
        }
        state.lastGlobalVolume = value;
        pushVcmd(events, nextEventId, Vcmd{VcmdGlobalVolume{mapItGlobalVolumeToNspcVolume(state.lastGlobalVolume)}});
        state.hasMeaningfulData = true;
        break;
    }
    case 'W': {
        const int slide = (value == 0) ? state.lastGlobalVolumeSlide : value;
        if (value != 0) {
            state.lastGlobalVolumeSlide = value;
        }
        const auto decoded = decodeItSlide(slide, rowTicks);
        if (!decoded.has_value()) {
            break;
        }
        state.lastGlobalVolume =
            std::clamp(state.lastGlobalVolume + (decoded->deltaPerTick * decoded->ticksApplied), 0, 128);
        pushVcmd(events, nextEventId, Vcmd{VcmdGlobalVolume{mapItGlobalVolumeToNspcVolume(state.lastGlobalVolume)}});
        state.hasMeaningfulData = true;
        break;
    }
    case 'M': {
        state.currentChannelVolume = std::clamp(value, 0, 64);
        const uint8_t volume =
            computeItVoiceVolume(state.currentNoteVolume, state.currentChannelVolume, module, sourceInstruments,
                                 state.currentInstrument);
        state.currentVolume = volume;
        // NSPC channel-volume-style changes don't always affect an already playing note unless
        // applied through a short fade step.
        pushVcmd(events, nextEventId, Vcmd{VcmdVolumeFade{
                                          .time = 1,
                                          .target = volume,
                                      }});
        state.hasMeaningfulData = true;
        break;
    }
    case 'X': {
        const uint8_t pan = mapItPanToNspc(value);
        pushVcmd(events, nextEventId, Vcmd{VcmdPanning{pan}});
        state.hasMeaningfulData = true;
        break;
    }
    case 'H': {
        emitVibrato(value);
        break;
    }
    case 'R': {
        emitTremolo(value);
        break;
    }
    case 'D':
    case 'K':
    case 'L': {
        const int slide = (value == 0) ? state.lastVolumeSlide : value;
        if (value != 0) {
            state.lastVolumeSlide = value;
        }
        applyVolumeSlide(slide);
        if (letter == 'K') {
            emitVibrato(0);
        } else if (letter == 'L' && state.lastPortamento > 0) {
            const bool forceRetriggerForPendingNote =
                state.forceRetriggerNextNoteAfterEfSlide && mappedNotePitch >= 0;
            if (forceRetriggerForPendingNote) {
                break;
            }
            emitSlideToNote(state.lastPortamento, true);
        }
        break;
    }
    case 'G': {
        if (value > 0) {
            state.lastPortamento = value;
        }
        const bool forceRetriggerForPendingNote =
            state.forceRetriggerNextNoteAfterEfSlide && mappedNotePitch >= 0;
        if (state.lastPortamento > 0 && !forceRetriggerForPendingNote) {
            emitSlideToNote(state.lastPortamento, true);
        }
        break;
    }
    case 'E': {
        const int slide = (value == 0) ? state.lastPortamentoDown : value;
        if (value != 0) {
            state.lastPortamentoDown = value;
        }
        const auto decoded = decodeItPitchSlide(slide, rowTicks);
        if (!decoded.has_value()) {
            break;
        }
        const int sourcePitch = (mappedNotePitch >= 0) ? mappedNotePitch : state.lastPlayedPitch;
        if (sourcePitch < 0) {
            break;
        }
        state.pitchSlideRemainder64 -= decoded->delta64;
        int semitoneSteps = 0;
        if (state.pitchSlideRemainder64 <= -64) {
            const int stepsDown = (-state.pitchSlideRemainder64) / 64;
            semitoneSteps = -stepsDown;
            state.pitchSlideRemainder64 += stepsDown * 64;
        }
        if (semitoneSteps == 0) {
            break;
        }
        const int targetPitch = sourcePitch + semitoneSteps;
        emitSlideToAbsoluteNote(sourcePitch, targetPitch, decoded->fineOrExtraFine ? 0 : 1,
                                decoded->fineOrExtraFine ? 1 : std::max(rowTicks - 1, 1));
        state.forceRetriggerNextNoteAfterEfSlide = true;
        break;
    }
    case 'F': {
        const int slide = (value == 0) ? state.lastPortamentoUp : value;
        if (value != 0) {
            state.lastPortamentoUp = value;
        }
        const auto decoded = decodeItPitchSlide(slide, rowTicks);
        if (!decoded.has_value()) {
            break;
        }
        const int sourcePitch = (mappedNotePitch >= 0) ? mappedNotePitch : state.lastPlayedPitch;
        if (sourcePitch < 0) {
            break;
        }
        state.pitchSlideRemainder64 += decoded->delta64;
        int semitoneSteps = 0;
        if (state.pitchSlideRemainder64 >= 64) {
            const int stepsUp = state.pitchSlideRemainder64 / 64;
            semitoneSteps = stepsUp;
            state.pitchSlideRemainder64 -= stepsUp * 64;
        }
        if (semitoneSteps == 0) {
            break;
        }
        const int targetPitch = sourcePitch + semitoneSteps;
        emitSlideToAbsoluteNote(sourcePitch, targetPitch, decoded->fineOrExtraFine ? 0 : 1,
                                decoded->fineOrExtraFine ? 1 : std::max(rowTicks - 1, 1));
        state.forceRetriggerNextNoteAfterEfSlide = true;
        break;
    }
    case 'J': {
        if (!extensionSupport.arpeggioVcmd.has_value()) {
            warnings.add("Arpeggio effect used but Arpeggio extension is unavailable; effect ignored");
            break;
        }
        int arpeggio = value;
        if (arpeggio == 0) {
            arpeggio = state.lastArpeggio;
        } else {
            state.lastArpeggio = arpeggio;
        }
        if (arpeggio == 0) {
            break;
        }
        result.usedArpeggio = true;
        if (state.arpeggioEnabled && state.activeArpeggio == arpeggio) {
            break;
        }
        VcmdExtension extension{};
        extension.id = *extensionSupport.arpeggioVcmd;
        extension.paramCount = 1;
        extension.params[0] = static_cast<uint8_t>(clampToByte(arpeggio));
        pushVcmd(events, nextEventId, Vcmd{extension});
        state.activeArpeggio = arpeggio;
        state.arpeggioEnabled = true;
        state.hasMeaningfulData = true;
        break;
    }
    case 'S': {
        const int subCommand = (value >> 4) & 0x0F;
        const int subValue = value & 0x0F;
        if (subCommand == 0x00 && subValue == 0x00) {
            pushVcmd(events, nextEventId, Vcmd{VcmdEchoOff{}});
            state.hasMeaningfulData = true;
            break;
        }
        if (subCommand == 0x0C) {
            const int cutTicks = (subValue == 0) ? 1 : subValue;
            if (cutTicks < rowTicks) {
                result.noteCutTicks = std::min(cutTicks, rowTicks - 1);
                state.hasMeaningfulData = true;
            }
            break;
        }
        if (subCommand == 0x0D) {
            const int delayTicks = (subValue == 0) ? 1 : subValue;
            result.noteDelayTicks = std::min(delayTicks, rowTicks);
            state.hasMeaningfulData = true;
            break;
        }
        if (subCommand == 0x08) {
            pushVcmd(events, nextEventId, Vcmd{VcmdPanning{static_cast<uint8_t>(std::clamp(0x14 - subValue, 0, 0x14))}});
            state.hasMeaningfulData = true;
            break;
        }
        warnings.add(std::format("Unsupported IT special effect S{:X}{:X} at {}", subCommand, subValue, rowLabel));
        break;
    }
    case 'Z': {
        if (value == 0) {
            pushVcmd(events, nextEventId, Vcmd{VcmdEchoOff{}});
        } else {
            const uint8_t level = static_cast<uint8_t>(std::clamp(value * 2, 0, 0x7F));
            pushVcmd(events, nextEventId, Vcmd{VcmdEchoOn{
                                              .channels = 0xFF,
                                              .left = level,
                                              .right = level,
                                          }});
        }
        state.hasMeaningfulData = true;
        break;
    }
    case 'C':
    case 'A':
        // handled elsewhere (row timing / pattern truncation)
        break;
    default:
        warnings.add(std::format("Unsupported IT effect '{}{:02X}' at {}", static_cast<char>(letter), value, rowLabel));
        break;
    }
    return result;
}

void seedTrackInitialQvIfMissing(NspcTrack& track, uint8_t qvByte);
void mergeContinuationTieSpans(NspcTrack& track);
void mergeClusteredVolumeVcmds(NspcTrack& track);

TrackConversionResult convertPatternChannelToTrack(const ItPattern& pattern, int sourcePatternIndex, int channel,
                                                   const RowTiming& timing, const ExtensionSupport& extensionSupport,
                                                   const ItModule& module,
                                                   const std::vector<ItInstrument>& sourceInstruments,
                                                   const std::vector<bool>& noteOffKeyOffByInstrument,
                                                   WarningCollector& warnings, int trackId, NspcEventId& nextEventId,
                                                   const TrackState& initialState) {
    NspcTrack track{};
    track.id = trackId;
    track.originalAddr = 0;
    TrackState state = initialState;

    const auto& cells = pattern.channels[static_cast<size_t>(channel)];
    const ItCell* firstCell = (timing.breakRow > 0 && !cells.empty()) ? &cells.front() : nullptr;
    const int firstLetter = (firstCell != nullptr && firstCell->command >= 0) ? (firstCell->command + 64) : -1;
    const auto firstRowVolumeEffect =
        (firstCell != nullptr) ? mapItVolumeColumnEffect(firstCell->volume) : std::optional<VolumeColumnMappedEffect>{};
    const int firstVolumeEffectLetter = firstRowVolumeEffect.has_value() ? firstRowVolumeEffect->letter : -1;
    const bool firstRowUsesVibrato = (firstLetter == 'H' || firstLetter == 'K' || firstVolumeEffectLetter == 'H');
    const bool firstRowUsesTremolo = (firstLetter == 'R');
    const bool firstRowUsesArpeggio = (firstLetter == 'J');

    if (state.pendingBoundaryVibratoOff && !firstRowUsesVibrato) {
        pushVcmd(track.events, nextEventId, Vcmd{VcmdVibratoOff{}});
    }
    if (state.pendingBoundaryTremoloOff && !firstRowUsesTremolo) {
        pushVcmd(track.events, nextEventId, Vcmd{VcmdTremoloOff{}});
    }
    if (state.pendingBoundaryArpeggioOff && !firstRowUsesArpeggio && extensionSupport.arpeggioVcmd.has_value()) {
        VcmdExtension extension{};
        extension.id = *extensionSupport.arpeggioVcmd;
        extension.paramCount = 1;
        extension.params[0] = 0;
        pushVcmd(track.events, nextEventId, Vcmd{extension});
    }
    state.pendingBoundaryVibratoOff = false;
    state.pendingBoundaryTremoloOff = false;
    state.pendingBoundaryArpeggioOff = false;

    struct EfSlideMergeState {
        bool active = false;
        size_t firstEventIndex = 0;
        int direction = 0;
        int lastTargetPitch = -1;
        int totalLength = 0;
    };
    EfSlideMergeState efSlideMerge{};

    for (int row = 0; row < timing.breakRow; ++row) {
        const ItCell& cell = cells[static_cast<size_t>(row)];
        const bool forceRetriggerPendingAtRowStart = state.forceRetriggerNextNoteAfterEfSlide;
        const bool hasPlayableNote = (cell.note >= 0 && cell.note < 120);
        const bool hasDirectVolumeColumn = (cell.volume >= 0 && cell.volume <= 64);
        const auto mappedVolumeColumnEffect = mapItVolumeColumnEffect(cell.volume);
        const int volumeColumnEffectLetter = mappedVolumeColumnEffect.has_value() ? mappedVolumeColumnEffect->letter : -1;
        const int effectLetter = (cell.command >= 0) ? (cell.command + 64) : -1;
        int mappedPitch = -1;
        if (hasPlayableNote) {
            mappedPitch = mapItNoteToNspcPitch(cell.note);
        }
        const int rowStartingPitch = state.lastPlayedPitch;

        if (cell.instrument > 0) {
            const int itInstrumentIndex = cell.instrument - 1;
            const int maxInstrumentCount = std::min(kMaxNspcAssets, static_cast<int>(sourceInstruments.size()));
            if (itInstrumentIndex >= 0 && itInstrumentIndex < maxInstrumentCount) {
                pushVcmd(track.events, nextEventId, Vcmd{VcmdInst{static_cast<uint8_t>(itInstrumentIndex)}});
                state.currentInstrument = itInstrumentIndex;
                state.hasMeaningfulData = true;
            } else {
                warnings.add(std::format("Instrument {:02X} out of supported NSPC range at pattern {:02X} ch {} row {}",
                                         cell.instrument, sourcePatternIndex, channel, row));
            }
        }

        // IT rows that provide only an instrument number (no note token, no direct volume column)
        // can reassert that instrument's baseline playback volume before row effects are applied.
        // Use a 1-tick fade here because VcmdVolume only affects the next triggered note.
        bool insertedInstrumentOnlyVolumeReset = false;
        if (cell.instrument > 0 && cell.note < 0 && !hasDirectVolumeColumn) {
            state.currentNoteVolume = resolveDefaultNoteVolume(module, sourceInstruments, state.currentInstrument);
            const uint8_t volume =
                computeItVoiceVolume(state.currentNoteVolume, state.currentChannelVolume, module, sourceInstruments,
                                     state.currentInstrument);
            state.currentVolume = volume;
            pushVcmd(track.events, nextEventId, Vcmd{VcmdVolumeFade{
                                             .time = 1,
                                             .target = volume,
                                         }});
            state.hasMeaningfulData = true;
            insertedInstrumentOnlyVolumeReset = true;
        }

        std::optional<size_t> noteVolumeVcmdIndex = std::nullopt;
        if (hasPlayableNote && cell.instrument > 0 && !hasDirectVolumeColumn) {
            state.currentNoteVolume = resolveDefaultNoteVolume(module, sourceInstruments, state.currentInstrument);
            const uint8_t volume =
                computeItVoiceVolume(state.currentNoteVolume, state.currentChannelVolume, module, sourceInstruments,
                                     state.currentInstrument);
            state.currentVolume = volume;
            noteVolumeVcmdIndex = track.events.size();
            pushVcmd(track.events, nextEventId, Vcmd{VcmdVolume{volume}});
            state.hasMeaningfulData = true;
        }

        std::optional<size_t> directVolumeVcmdIndex = std::nullopt;
        if (cell.volume >= 0 && cell.volume <= 64) {
            state.currentNoteVolume = cell.volume;
            const uint8_t volume =
                computeItVoiceVolume(state.currentNoteVolume, state.currentChannelVolume, module, sourceInstruments,
                                     state.currentInstrument);
            state.currentVolume = volume;
            directVolumeVcmdIndex = track.events.size();
            if (cell.note < 0) {
                pushVcmd(track.events, nextEventId, Vcmd{VcmdVolumeFade{
                                                 .time = 1,
                                                 .target = volume,
                                             }});
            } else {
                pushVcmd(track.events, nextEventId, Vcmd{VcmdVolume{volume}});
            }
            state.hasMeaningfulData = true;
        } else if (cell.volume >= 128 && cell.volume <= 192) {
            const int panValue = (cell.volume == 128) ? 0 : ((cell.volume - 128) * 4) - 1;
            const uint8_t pan = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(0x14 - (panValue / 12.8))),
                                                                0, 0x14));
            pushVcmd(track.events, nextEventId, Vcmd{VcmdPanning{pan}});
            state.hasMeaningfulData = true;
        }

        const size_t effectEventStart = track.events.size();
        EffectApplyResult effectResult{};
        if (mappedVolumeColumnEffect.has_value()) {
            ItCell volumeEffectCell{};
            volumeEffectCell.command = mappedVolumeColumnEffect->letter - 64;
            int volumeEffectValue = mappedVolumeColumnEffect->value;
            if (mappedVolumeColumnEffect->useLastVibratoRate) {
                volumeEffectValue = ((std::clamp(state.lastVibratoRate, 0, 0x0F) << 4) | (volumeEffectValue & 0x0F));
            }
            volumeEffectCell.value = volumeEffectValue;
            const EffectApplyResult volumeEffectResult =
                applyItEffect(volumeEffectCell, mappedPitch, track.events, nextEventId, state, extensionSupport, module,
                              sourceInstruments, warnings, sourcePatternIndex, channel, row,
                              static_cast<int>(timing.ticks[static_cast<size_t>(row)]));
            mergeEffectApplyResult(effectResult, volumeEffectResult);
        }
        const EffectApplyResult commandEffectResult =
            applyItEffect(cell, mappedPitch, track.events, nextEventId, state, extensionSupport, module,
                          sourceInstruments, warnings, sourcePatternIndex, channel, row,
                          static_cast<int>(timing.ticks[static_cast<size_t>(row)]));
        mergeEffectApplyResult(effectResult, commandEffectResult);

        // When an instrument-only row performs a baseline reset via a 1-tick volume fade,
        // defer any same-row long fade (e.g. Dxy) to tick 1 so both operations take effect.
        std::optional<VcmdVolumeFade> deferredRowVolumeFade = std::nullopt;
        if (insertedInstrumentOnlyVolumeReset) {
            for (size_t i = effectEventStart; i < track.events.size(); ++i) {
                auto* vcmd = std::get_if<Vcmd>(&track.events[i].event);
                if (vcmd == nullptr || !std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
                    continue;
                }
                auto fade = std::get<VcmdVolumeFade>(vcmd->vcmd);
                if (fade.time <= 1) {
                    continue;
                }
                fade.time = static_cast<uint8_t>(std::clamp(static_cast<int>(fade.time) - 1, 1, 0xFF));
                deferredRowVolumeFade = fade;
                track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }

        std::optional<size_t> rowPitchSlideEventIndex = std::nullopt;
        std::optional<VcmdPitchSlideToNote> rowPitchSlideEvent = std::nullopt;
        for (size_t i = effectEventStart; i < track.events.size(); ++i) {
            const auto* vcmd = std::get_if<Vcmd>(&track.events[i].event);
            if (vcmd == nullptr || !std::holds_alternative<VcmdPitchSlideToNote>(vcmd->vcmd)) {
                continue;
            }
            rowPitchSlideEventIndex = i;
            rowPitchSlideEvent = std::get<VcmdPitchSlideToNote>(vcmd->vcmd);
            break;
        }

        if (hasPlayableNote && state.vibratoEnabled && !effectResult.usedVibrato) {
            pushVcmd(track.events, nextEventId, Vcmd{VcmdVibratoOff{}});
            state.vibratoEnabled = false;
            state.activeVibratoRate = 0;
            state.activeVibratoDepth = 0;
        }
        if (hasPlayableNote && state.tremoloEnabled && !effectResult.usedTremolo) {
            pushVcmd(track.events, nextEventId, Vcmd{VcmdTremoloOff{}});
            state.tremoloEnabled = false;
            state.activeTremoloRate = 0;
            state.activeTremoloDepth = 0;
        }
        if (hasPlayableNote && state.arpeggioEnabled && !effectResult.usedArpeggio &&
            extensionSupport.arpeggioVcmd.has_value()) {
            VcmdExtension extension{};
            extension.id = *extensionSupport.arpeggioVcmd;
            extension.paramCount = 1;
            extension.params[0] = 0;
            pushVcmd(track.events, nextEventId, Vcmd{extension});
            state.arpeggioEnabled = false;
            state.activeArpeggio = 0;
        }

        const int rowTicks = static_cast<int>(timing.ticks[static_cast<size_t>(row)]);
        enum class ItNoteKind { None, Note, NoteOff, NoteCut, Other };
        const ItNoteKind noteKind = [&]() {
            if (cell.note < 0) {
                return ItNoteKind::None;
            }
            if (cell.note < 120) {
                return ItNoteKind::Note;
            }
            if (cell.note == kItNoteOffValue) {
                return ItNoteKind::NoteOff;
            }
            if (cell.note == kItNoteCutValue) {
                return ItNoteKind::NoteCut;
            }
            return ItNoteKind::Other;
        }();
        const bool hasNoteCommand = noteKind != ItNoteKind::None;
        const bool noteCommandInRange = noteKind == ItNoteKind::Note;
        const bool noteCommandIsRest = (noteKind == ItNoteKind::NoteCut);
        const bool noteCommandIsNoteOff = (noteKind == ItNoteKind::NoteOff);
        const bool noteCommandUsesKeyOff = noteCommandIsRest ||
                                           (noteCommandIsNoteOff &&
                                            instrumentNoteOffUsesKeyOff(noteOffKeyOffByInstrument,
                                                                        state.currentInstrument));

        int delayTicks = -1;
        if (noteCommandInRange || noteCommandIsRest || noteCommandIsNoteOff) {
            delayTicks = effectResult.noteDelayTicks;
        }
        if (delayTicks < 0 || delayTicks > rowTicks) {
            delayTicks = -1;
        }
        const bool forceRetriggerAfterEfSlide =
            forceRetriggerPendingAtRowStart && noteCommandInRange && delayTicks != rowTicks;
        const bool retriggersNoteThisRow =
            noteCommandInRange &&
            (forceRetriggerAfterEfSlide || (!effectResult.consumeCurrentRowNoteAsPortamentoTarget && delayTicks != rowTicks));
        auto convertRowVolumeToShortFade = [&](std::optional<size_t> eventIndex) {
            if (!eventIndex.has_value() || *eventIndex >= track.events.size() || retriggersNoteThisRow) {
                return;
            }
            auto* volumeEvent = std::get_if<Vcmd>(&track.events[*eventIndex].event);
            if (volumeEvent == nullptr || !std::holds_alternative<VcmdVolume>(volumeEvent->vcmd)) {
                return;
            }
            const uint8_t target = std::get<VcmdVolume>(volumeEvent->vcmd).volume;
            volumeEvent->vcmd = VcmdVolumeFade{
                .time = 1,
                .target = target,
            };
        };
        convertRowVolumeToShortFade(noteVolumeVcmdIndex);
        convertRowVolumeToShortFade(directVolumeVcmdIndex);
        int cutTicks = effectResult.noteCutTicks;
        if (cutTicks <= 0 || cutTicks >= rowTicks) {
            cutTicks = -1;
        }
        const bool noteTriggersAtRowStart = (delayTicks <= 0);
        const bool rowUsesEfPitchSlide =
            (effectLetter == 'E' || effectLetter == 'F' || volumeColumnEffectLetter == 'E' ||
             volumeColumnEffectLetter == 'F');

        const int rowSlideSourcePitch = (mappedPitch >= 0) ? mappedPitch : rowStartingPitch;
        const bool canMergeEfSlideThisRow =
            rowUsesEfPitchSlide && rowPitchSlideEventIndex.has_value() && rowPitchSlideEvent.has_value() &&
            rowPitchSlideEvent->delay == 1 && rowPitchSlideEvent->length > 0 && !retriggersNoteThisRow &&
            cutTicks < 0 && delayTicks < 0 && rowSlideSourcePitch >= 0;
        if (canMergeEfSlideThisRow) {
            const int rowSlideTargetPitch = static_cast<int>(rowPitchSlideEvent->note);
            const int rowSlideDirection =
                (rowSlideTargetPitch > rowSlideSourcePitch) ? 1 : ((rowSlideTargetPitch < rowSlideSourcePitch) ? -1 : 0);
            const int rowSlideLength = static_cast<int>(rowPitchSlideEvent->length);
            if (rowSlideDirection != 0 && rowSlideLength > 0) {
                bool merged = false;
                if (efSlideMerge.active && efSlideMerge.direction == rowSlideDirection &&
                    efSlideMerge.lastTargetPitch == rowSlideSourcePitch &&
                    (efSlideMerge.totalLength + rowSlideLength) <= 0xFF) {
                    auto* firstSlideVcmd = std::get_if<Vcmd>(&track.events[efSlideMerge.firstEventIndex].event);
                    if (firstSlideVcmd != nullptr && std::holds_alternative<VcmdPitchSlideToNote>(firstSlideVcmd->vcmd)) {
                        auto mergedSlide = std::get<VcmdPitchSlideToNote>(firstSlideVcmd->vcmd);
                        mergedSlide.length =
                            static_cast<uint8_t>(std::clamp(efSlideMerge.totalLength + rowSlideLength, 1, 0xFF));
                        mergedSlide.note = static_cast<uint8_t>(rowSlideTargetPitch);
                        firstSlideVcmd->vcmd = mergedSlide;
                        track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(*rowPitchSlideEventIndex));
                        rowPitchSlideEventIndex = std::nullopt;
                        rowPitchSlideEvent = std::nullopt;
                        efSlideMerge.lastTargetPitch = rowSlideTargetPitch;
                        efSlideMerge.totalLength += rowSlideLength;
                        merged = true;
                    } else {
                        efSlideMerge.active = false;
                    }
                }
                if (!merged) {
                    efSlideMerge.active = true;
                    efSlideMerge.firstEventIndex = *rowPitchSlideEventIndex;
                    efSlideMerge.direction = rowSlideDirection;
                    efSlideMerge.lastTargetPitch = rowSlideTargetPitch;
                    efSlideMerge.totalLength = rowSlideLength;
                }
            } else {
                efSlideMerge.active = false;
            }
        } else {
            efSlideMerge.active = false;
        }

        const bool deferNormalEfSlideToTickOne =
            hasPlayableNote && retriggersNoteThisRow && noteTriggersAtRowStart && cutTicks < 0 && rowUsesEfPitchSlide &&
            rowPitchSlideEvent.has_value() &&
            rowPitchSlideEvent->delay == 1 && rowTicks > 1;
        std::optional<VcmdPitchSlideToNote> deferredRowPitchSlide = std::nullopt;
        bool deferredRowPitchSlideEmitted = false;
        bool deferredRowVolumeFadeEmitted = false;
        if (deferNormalEfSlideToTickOne && rowPitchSlideEventIndex.has_value()) {
            deferredRowPitchSlide = rowPitchSlideEvent;
            deferredRowPitchSlide->delay = 0;
            track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(*rowPitchSlideEventIndex));
        }

        enum class RowEventMode { Tie, Rest, Note };
        const uint8_t mappedPitchByte =
            static_cast<uint8_t>(hasPlayableNote ? mapItNoteToNspcPitch(cell.note) : 0);
        const RowEventMode triggeredMode =
            hasNoteCommand ? (noteCommandInRange ? ((effectResult.consumeCurrentRowNoteAsPortamentoTarget &&
                                                     !forceRetriggerAfterEfSlide)
                                                        ? RowEventMode::Tie
                                                        : RowEventMode::Note)
                                                 : (noteCommandUsesKeyOff ? RowEventMode::Rest : RowEventMode::Tie))
                           : RowEventMode::Tie;

        auto emitRowSegment = [&](int ticks, RowEventMode mode) {
            if (ticks <= 0) {
                return;
            }
            pushEvent(track.events, nextEventId, NspcEvent{Duration{
                                                     .ticks = static_cast<uint8_t>(std::clamp(ticks, 1, 0x7F)),
                                                     .quantization = std::nullopt,
                                                     .velocity = std::nullopt,
                                                 }});
            switch (mode) {
            case RowEventMode::Tie:
                pushEvent(track.events, nextEventId, NspcEvent{Tie{}});
                break;
            case RowEventMode::Rest:
                pushEvent(track.events, nextEventId, NspcEvent{Rest{}});
                break;
            case RowEventMode::Note:
                pushEvent(track.events, nextEventId, NspcEvent{Note{.pitch = mappedPitchByte}});
                break;
            }
        };

        std::vector<std::pair<int, RowEventMode>> boundaries;
        boundaries.reserve(3);
        if (cutTicks > 0) {
            boundaries.push_back({cutTicks, RowEventMode::Rest});
        }
        if (delayTicks >= 0 && delayTicks < rowTicks) {
            boundaries.push_back({delayTicks, triggeredMode});
        }
        if (deferredRowPitchSlide.has_value()) {
            boundaries.push_back({1, RowEventMode::Tie});
        }
        if (deferredRowVolumeFade.has_value()) {
            boundaries.push_back({1, RowEventMode::Tie});
        }
        std::sort(boundaries.begin(), boundaries.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        RowEventMode currentMode = (delayTicks <= 0) ? triggeredMode : RowEventMode::Tie;
        int cursor = 0;
        for (const auto& [tick, modeAfterTick] : boundaries) {
            if (tick <= cursor) {
                currentMode = modeAfterTick;
                continue;
            }
            emitRowSegment(tick - cursor, currentMode);
            cursor = tick;
            currentMode = modeAfterTick;
            if (!deferredRowVolumeFadeEmitted && deferredRowVolumeFade.has_value() && cursor >= 1) {
                pushVcmd(track.events, nextEventId, Vcmd{*deferredRowVolumeFade});
                deferredRowVolumeFadeEmitted = true;
            }
            if (!deferredRowPitchSlideEmitted && deferredRowPitchSlide.has_value() && cursor >= 1) {
                pushVcmd(track.events, nextEventId, Vcmd{*deferredRowPitchSlide});
                deferredRowPitchSlideEmitted = true;
            }
        }
        emitRowSegment(rowTicks - cursor, currentMode);

        const bool noteWasTriggeredThisRow = hasNoteCommand && (delayTicks != rowTicks);
        if (noteWasTriggeredThisRow) {
            if (forceRetriggerAfterEfSlide) {
                state.forceRetriggerNextNoteAfterEfSlide = false;
            }
            if (triggeredMode == RowEventMode::Note) {
                state.lastPlayedPitch = mapItNoteToNspcPitch(cell.note);
            } else if (triggeredMode == RowEventMode::Rest) {
                state.lastPlayedPitch = -1;
            }
            state.hasMeaningfulData = true;
        }

        if (effectResult.emittedPitchEnvelope) {
            // F1/F2 persist on N-SPC engines; E/F IT slides are row-local, so disable
            // the envelope at row end to avoid affecting subsequent notes.
            pushVcmd(track.events, nextEventId, Vcmd{VcmdPitchEnvelopeOff{}});
        }
        if (effectResult.resultingPitchAfterRow.has_value()) {
            state.lastPlayedPitch = *effectResult.resultingPitchAfterRow;
        }
    }

    if (state.vibratoEnabled) {
        pushVcmd(track.events, nextEventId, Vcmd{VcmdVibratoOff{}});
        state.vibratoEnabled = false;
        state.activeVibratoRate = 0;
        state.activeVibratoDepth = 0;
        state.pendingBoundaryVibratoOff = true;
    }
    if (state.tremoloEnabled) {
        pushVcmd(track.events, nextEventId, Vcmd{VcmdTremoloOff{}});
        state.tremoloEnabled = false;
        state.activeTremoloRate = 0;
        state.activeTremoloDepth = 0;
        state.pendingBoundaryTremoloOff = true;
    }
    if (state.arpeggioEnabled && extensionSupport.arpeggioVcmd.has_value()) {
        VcmdExtension extension{};
        extension.id = *extensionSupport.arpeggioVcmd;
        extension.paramCount = 1;
        extension.params[0] = 0;
        pushVcmd(track.events, nextEventId, Vcmd{extension});
        state.arpeggioEnabled = false;
        state.activeArpeggio = 0;
        state.pendingBoundaryArpeggioOff = true;
    }

    mergeContinuationTieSpans(track);
    mergeClusteredVolumeVcmds(track);
    seedTrackInitialQvIfMissing(track, kDefaultQvByte);
    pushEvent(track.events, nextEventId, NspcEvent{End{}});
    return TrackConversionResult{
        .track = std::move(track),
        .endState = state,
    };
}

bool patternHasIgnoredChannels(const ItPattern& pattern) {
    for (int channel = kMaxNspcChannels; channel < static_cast<int>(pattern.channels.size()); ++channel) {
        const auto& rows = pattern.channels[static_cast<size_t>(channel)];
        if (std::any_of(rows.begin(), rows.end(), [](const ItCell& cell) { return cell.hasAnyData(); })) {
            return true;
        }
    }
    return false;
}

bool channelHasAnyData(const ItPattern& pattern, int channel, int breakRow) {
    if (channel < 0 || channel >= static_cast<int>(pattern.channels.size())) {
        return false;
    }
    const auto& rows = pattern.channels[static_cast<size_t>(channel)];
    const int limit = std::min<int>(breakRow, static_cast<int>(rows.size()));
    for (int row = 0; row < limit; ++row) {
        if (rows[static_cast<size_t>(row)].hasAnyData()) {
            return true;
        }
    }
    return false;
}

void mergeClusteredVolumeVcmds(NspcTrack& track) {
    // Pass 1: reduce adjacent same-tick volume-command clusters without removing
    // a Volume that intentionally sets the fade start level for a following VolumeFade.
    std::vector<size_t> eraseIndices;
    std::vector<size_t> clusteredVolumeIndices;
    clusteredVolumeIndices.reserve(4);

    auto isVolumeAt = [&](size_t index) {
        const auto* vcmd = std::get_if<Vcmd>(&track.events[index].event);
        return vcmd != nullptr && std::holds_alternative<VcmdVolume>(vcmd->vcmd);
    };
    auto isVolumeFadeAt = [&](size_t index) {
        const auto* vcmd = std::get_if<Vcmd>(&track.events[index].event);
        return vcmd != nullptr && std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd);
    };

    auto flushCluster = [&]() {
        if (clusteredVolumeIndices.empty()) {
            return;
        }
        if (clusteredVolumeIndices.size() == 1) {
            clusteredVolumeIndices.clear();
            return;
        }
        std::vector<size_t> keepIndices;
        keepIndices.reserve(clusteredVolumeIndices.size());
        std::optional<size_t> pendingVolume = std::nullopt;
        for (size_t index : clusteredVolumeIndices) {
            if (isVolumeAt(index)) {
                pendingVolume = index; // Keep only the latest pending absolute volume.
                continue;
            }
            if (!isVolumeFadeAt(index)) {
                continue;
            }

            if (pendingVolume.has_value()) {
                keepIndices.push_back(*pendingVolume);
                pendingVolume = std::nullopt;
            }

            if (!keepIndices.empty() && isVolumeFadeAt(keepIndices.back())) {
                keepIndices.back() = index; // Latest fade overrides previous fade in same tick cluster.
            } else {
                keepIndices.push_back(index);
            }
        }
        if (pendingVolume.has_value()) {
            keepIndices.push_back(*pendingVolume);
        }

        std::unordered_set<size_t> keepSet(keepIndices.begin(), keepIndices.end());
        for (size_t index : clusteredVolumeIndices) {
            if (!keepSet.contains(index)) {
                eraseIndices.push_back(index);
            }
        }
        clusteredVolumeIndices.clear();
    };

    for (size_t i = 0; i < track.events.size(); ++i) {
        const auto* vcmd = std::get_if<Vcmd>(&track.events[i].event);
        if (vcmd == nullptr) {
            flushCluster();
            continue;
        }
        if (std::holds_alternative<VcmdVolume>(vcmd->vcmd) || std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
            clusteredVolumeIndices.push_back(i);
        } else {
            flushCluster();
        }
    }
    flushCluster();

    std::sort(eraseIndices.begin(), eraseIndices.end());
    eraseIndices.erase(std::unique(eraseIndices.begin(), eraseIndices.end()), eraseIndices.end());
    for (auto it = eraseIndices.rbegin(); it != eraseIndices.rend(); ++it) {
        track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(*it));
    }

    // Pass 2: merge chained fades across elapsed ticks when each fade starts exactly
    // where the previous fade ended, with no intervening non-volume VCMD.
    struct FadeChain {
        bool active = false;
        size_t firstIndex = 0;
        uint32_t endTick = 0;
        int totalTime = 0;
        uint8_t finalTarget = 0;
        std::vector<size_t> indices;
    };

    FadeChain chain{};
    std::vector<size_t> chainedEraseIndices;
    uint32_t currentTick = 0;

    auto flushChain = [&]() {
        if (!chain.active) {
            return;
        }
        if (chain.indices.size() > 1) {
            auto* firstVcmd = std::get_if<Vcmd>(&track.events[chain.firstIndex].event);
            if (firstVcmd != nullptr && std::holds_alternative<VcmdVolumeFade>(firstVcmd->vcmd)) {
                firstVcmd->vcmd = VcmdVolumeFade{
                    .time = static_cast<uint8_t>(std::clamp(chain.totalTime, 1, 0xFF)),
                    .target = chain.finalTarget,
                };
                chainedEraseIndices.insert(chainedEraseIndices.end(), chain.indices.begin() + 1, chain.indices.end());
            }
        }
        chain = FadeChain{};
    };

    auto startChain = [&](size_t index, const VcmdVolumeFade& fade) {
        chain.active = true;
        chain.firstIndex = index;
        chain.endTick = currentTick + fade.time;
        chain.totalTime = fade.time;
        chain.finalTarget = fade.target;
        chain.indices.clear();
        chain.indices.push_back(index);
    };

    for (size_t i = 0; i < track.events.size(); ++i) {
        if (const auto* duration = std::get_if<Duration>(&track.events[i].event); duration != nullptr) {
            currentTick += duration->ticks;
            continue;
        }

        const auto* vcmd = std::get_if<Vcmd>(&track.events[i].event);
        if (vcmd == nullptr) {
            continue;
        }

        if (std::holds_alternative<VcmdVolume>(vcmd->vcmd)) {
            flushChain();
            continue;
        }
        if (!std::holds_alternative<VcmdVolumeFade>(vcmd->vcmd)) {
            flushChain();
            continue;
        }

        const auto fade = std::get<VcmdVolumeFade>(vcmd->vcmd);
        if (fade.time == 0) {
            flushChain();
            continue;
        }

        if (!chain.active) {
            startChain(i, fade);
            continue;
        }

        if (currentTick == chain.endTick && chain.totalTime > 1 && fade.time > 1 &&
            (chain.totalTime + static_cast<int>(fade.time)) <= 0xFF) {
            chain.totalTime += fade.time;
            chain.endTick = currentTick + fade.time;
            chain.finalTarget = fade.target;
            chain.indices.push_back(i);
            continue;
        }

        flushChain();
        startChain(i, fade);
    }
    flushChain();

    std::sort(chainedEraseIndices.begin(), chainedEraseIndices.end());
    chainedEraseIndices.erase(std::unique(chainedEraseIndices.begin(), chainedEraseIndices.end()),
                              chainedEraseIndices.end());
    for (auto it = chainedEraseIndices.rbegin(); it != chainedEraseIndices.rend(); ++it) {
        track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(*it));
    }
}

void mergeContinuationTieSpans(NspcTrack& track) {
    auto isTimedRowEvent = [](const NspcEvent& event) {
        return std::holds_alternative<Note>(event) || std::holds_alternative<Percussion>(event) ||
               std::holds_alternative<Tie>(event) || std::holds_alternative<Rest>(event);
    };
    auto buildEventTicks = [&]() {
        std::vector<uint32_t> ticks(track.events.size(), 0);
        uint32_t currentTick = 0;
        Duration currentDuration{.ticks = 1, .quantization = std::nullopt, .velocity = std::nullopt};
        for (size_t index = 0; index < track.events.size(); ++index) {
            ticks[index] = currentTick;
            const auto& event = track.events[index].event;
            if (const auto* duration = std::get_if<Duration>(&event); duration != nullptr) {
                currentDuration = *duration;
                continue;
            }
            if (isTimedRowEvent(event)) {
                currentTick += currentDuration.ticks;
            }
        }
        return ticks;
    };

    if (track.events.size() < 4) {
        return;
    }

    for (size_t i = 0; i + 3 < track.events.size();) {
        auto* leadDuration = std::get_if<Duration>(&track.events[i].event);
        if (leadDuration == nullptr || !isTimedRowEvent(track.events[i + 1].event)) {
            ++i;
            continue;
        }

        auto* continuationDuration = std::get_if<Duration>(&track.events[i + 2].event);
        if (continuationDuration == nullptr || !std::holds_alternative<Tie>(track.events[i + 3].event)) {
            ++i;
            continue;
        }
        if (continuationDuration->quantization.has_value() || continuationDuration->velocity.has_value()) {
            ++i;
            continue;
        }
        const auto ticks = buildEventTicks();
        const uint32_t continuationStartTick = ticks[i + 2];
        const uint32_t continuationEndTick = continuationStartTick + continuationDuration->ticks;
        bool hasRowStateChanges = false;
        for (size_t j = 0; j < track.events.size(); ++j) {
            if (j == i + 2 || j == i + 3) {
                continue;
            }
            const uint32_t eventTick = ticks[j];
            if (eventTick >= continuationStartTick && eventTick < continuationEndTick) {
                hasRowStateChanges = true;
                break;
            }
        }
        if (hasRowStateChanges) {
            ++i;
            continue;
        }

        const int leadTicks = static_cast<int>(leadDuration->ticks);
        const int continuationTicks = static_cast<int>(continuationDuration->ticks);
        const int room = 0x7F - leadTicks;
        const int absorbedTicks = std::clamp(continuationTicks, 0, room);
        if (absorbedTicks <= 0) {
            i += 2;
            continue;
        }

        leadDuration->ticks = static_cast<uint8_t>(leadTicks + absorbedTicks);
        continuationDuration->ticks = static_cast<uint8_t>(continuationTicks - absorbedTicks);
        if (continuationDuration->ticks == 0) {
            track.events.erase(track.events.begin() + static_cast<std::ptrdiff_t>(i + 2),
                               track.events.begin() + static_cast<std::ptrdiff_t>(i + 4));
            continue;
        }

        i += 2;
    }
}

void seedTrackInitialQvIfMissing(NspcTrack& track, uint8_t qvByte) {
    for (auto& entry : track.events) {
        auto* duration = std::get_if<Duration>(&entry.event);
        if (duration == nullptr) {
            continue;
        }
        if (!duration->quantization.has_value() && !duration->velocity.has_value()) {
            duration->quantization = static_cast<uint8_t>((qvByte >> 4) & 0x07);
            duration->velocity = static_cast<uint8_t>(qvByte & 0x0F);
        }
        return;
    }
}

NspcTrack* findTrackById(std::vector<NspcTrack>& tracks, int trackId) {
    auto it = std::find_if(tracks.begin(), tracks.end(), [&](const NspcTrack& track) {
        return track.id == trackId;
    });
    if (it == tracks.end()) {
        return nullptr;
    }
    return &(*it);
}

void prependVcmds(NspcTrack& track, std::span<const Vcmd> commands, NspcEventId& nextEventId) {
    std::vector<NspcEventEntry> prefix;
    prefix.reserve(commands.size());
    for (const Vcmd& command : commands) {
        prefix.push_back(NspcEventEntry{
            .id = nextEventId++,
            .event = NspcEvent{command},
            .originalAddr = std::nullopt,
        });
    }
    track.events.insert(track.events.begin(), prefix.begin(), prefix.end());
}

void injectInitialSequenceState(NspcSong& song, const std::vector<int>& orderedSongPatternIds, const ItModule& module,
                                const std::vector<ItInstrument>& sourceInstruments,
                                const ExtensionSupport& extensionSupport, NspcEventId& nextEventId,
                                WarningCollector& warnings) {
    if (orderedSongPatternIds.empty()) {
        return;
    }

    std::array<bool, kMaxNspcChannels> initializedChannels{};
    initializedChannels.fill(false);
    bool initializedGlobal = false;
    bool firstPatternPass = true;

    for (const int songPatternId : orderedSongPatternIds) {
        auto patternIt = std::find_if(song.patterns().begin(), song.patterns().end(), [&](const NspcPattern& pattern) {
            return pattern.id == songPatternId;
        });
        if (patternIt == song.patterns().end() || !patternIt->channelTrackIds.has_value()) {
            continue;
        }

        const auto& trackIds = *patternIt->channelTrackIds;
        for (int channel = 0; channel < kMaxNspcChannels; ++channel) {
            if (initializedChannels[static_cast<size_t>(channel)]) {
                continue;
            }
            const int trackId = trackIds[static_cast<size_t>(channel)];
            if (trackId < 0) {
                continue;
            }
            NspcTrack* track = findTrackById(song.tracks(), trackId);
            if (track == nullptr) {
                continue;
            }

            const int channelPan =
                (channel < static_cast<int>(module.initialChannelPanning.size()))
                    ? std::clamp(module.initialChannelPanning[static_cast<size_t>(channel)], 0, 64)
                    : 32;
            const int channelVolume =
                (channel < static_cast<int>(module.initialChannelVolume.size()))
                    ? std::clamp(module.initialChannelVolume[static_cast<size_t>(channel)], 0, 64)
                    : 64;
            const uint8_t startupVolume =
                computeItVoiceVolume(64, channelVolume, module, sourceInstruments, -1);

            std::vector<Vcmd> channelStartup;
            channelStartup.reserve(4);
            channelStartup.push_back(Vcmd{VcmdPanning{mapItPanToNspc(channelPan)}});
            channelStartup.push_back(Vcmd{VcmdVolume{startupVolume}});
            if (extensionSupport.legatoVcmd.has_value()) {
                VcmdExtension extension{};
                extension.id = *extensionSupport.legatoVcmd;
                extension.paramCount = 1;
                extension.params[0] = 1;
                channelStartup.push_back(Vcmd{extension});
            }
            if (firstPatternPass && extensionSupport.noPatternKoffVcmd.has_value()) {
                VcmdExtension extension{};
                extension.id = *extensionSupport.noPatternKoffVcmd;
                extension.paramCount = 1;
                extension.params[0] = 1;
                channelStartup.push_back(Vcmd{extension});
            }
            prependVcmds(*track, channelStartup, nextEventId);
            initializedChannels[static_cast<size_t>(channel)] = true;
        }

        if (!initializedGlobal) {
            const auto primaryTrackIt = std::find_if(trackIds.begin(), trackIds.end(), [](int trackId) {
                return trackId >= 0;
            });
            if (primaryTrackIt != trackIds.end()) {
                NspcTrack* track = findTrackById(song.tracks(), *primaryTrackIt);
                if (track != nullptr) {
                    std::vector<Vcmd> startup;
                    startup.reserve(3);
                    startup.push_back(Vcmd{VcmdEchoOff{}});
                    startup.push_back(Vcmd{VcmdGlobalVolume{mapItGlobalVolumeToNspcVolume(module.globalVolume)}});
                    startup.push_back(Vcmd{VcmdTempo{mapItTempoToNspcTempo(module.initialTempo)}});
                    prependVcmds(*track, startup, nextEventId);
                    initializedGlobal = true;
                }
            }
        }

        if (initializedGlobal &&
            std::all_of(initializedChannels.begin(), initializedChannels.end(), [](bool value) { return value; })) {
            break;
        }
        firstPatternPass = false;
    }

    if (!initializedGlobal) {
        warnings.add("Failed to inject IT initial global state (tempo/global volume); no playable track in sequence");
    }
}

uint32_t estimateFreeAfterDeletion(const NspcProject& baseProject, const ItImportOptions& options) {
    NspcProject working = baseProject;
    std::set<int> sampleDeletes(options.samplesToDelete.begin(), options.samplesToDelete.end());
    std::set<int> instrumentDeletes(options.instrumentsToDelete.begin(), options.instrumentsToDelete.end());

    auto& instruments = working.instruments();
    auto instIt = instruments.begin();
    while (instIt != instruments.end()) {
        if (!instrumentDeletes.contains(instIt->id)) {
            ++instIt;
            continue;
        }
        sampleDeletes.insert(instIt->sampleIndex & 0x7F);
        instIt = instruments.erase(instIt);
    }

    std::set<int> inUseSamples;
    for (const auto& instrument : working.instruments()) {
        inUseSamples.insert(instrument.sampleIndex & 0x7F);
    }
    auto& samples = working.samples();
    auto sampleIt = samples.begin();
    while (sampleIt != samples.end()) {
        if (!sampleDeletes.contains(sampleIt->id) || inUseSamples.contains(sampleIt->id)) {
            ++sampleIt;
            continue;
        }
        sampleIt = samples.erase(sampleIt);
    }

    working.refreshAramUsage();
    return working.aramUsage().freeBytes;
}

std::expected<std::vector<uint8_t>, std::string> readFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Failed to open '{}'", path.string()));
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

std::expected<ItModule, std::string> loadItModuleFromPath(const std::filesystem::path& path,
                                                          WarningCollector& warnings) {
    auto fileBytes = readFileBytes(path);
    if (!fileBytes.has_value()) {
        return std::unexpected(fileBytes.error());
    }
    return parseItModule(*fileBytes, warnings);
}

struct PreparedItAssets {
    std::vector<ConvertedSampleInfo> convertedSamples;
    std::vector<ItInstrument> instruments;
    int instrumentLimit = 0;
};

std::expected<PreparedItAssets, std::string> prepareItAssets(const ItModule& module, const ItImportOptions& options,
                                                             WarningCollector& warnings) {
    auto convertedSamples = convertItSamples(module, options, warnings);
    if (!convertedSamples.has_value()) {
        return std::unexpected(convertedSamples.error());
    }

    std::vector<ItInstrument> instruments = materializeItInstruments(module, convertedSamples->size(), warnings);
    const int instrumentLimit = std::min<int>(static_cast<int>(instruments.size()), kMaxNspcAssets);
    if (static_cast<int>(instruments.size()) > instrumentLimit) {
        warnings.add(std::format("IT has {} instruments; only first {} are imported", instruments.size(), instrumentLimit));
    }

    return PreparedItAssets{
        .convertedSamples = std::move(*convertedSamples),
        .instruments = std::move(instruments),
        .instrumentLimit = instrumentLimit,
    };
}

bool trackHasNonTimingData(const NspcTrack& track) {
    for (const auto& entry : track.events) {
        if (std::holds_alternative<End>(entry.event)) {
            continue;
        }
        if (std::holds_alternative<Duration>(entry.event) || std::holds_alternative<Tie>(entry.event)) {
            continue;
        }
        return true;
    }
    return false;
}

}  // namespace

std::expected<ItImportPreview, std::string>
analyzeItFileForSongSlot(const NspcProject& baseProject, const std::filesystem::path& itPath, int targetSongIndex,
                         const ItImportOptions& options) {
    if (targetSongIndex < 0 || targetSongIndex >= static_cast<int>(baseProject.songs().size())) {
        return std::unexpected("Target song index is out of range");
    }

    WarningCollector warnings;
    auto parsedModule = loadItModuleFromPath(itPath, warnings);
    if (!parsedModule.has_value()) {
        return std::unexpected(parsedModule.error());
    }
    ItModule module = std::move(*parsedModule);

    NspcProject extensionProbe = baseProject;
    const ExtensionSupport extensionSupport = enableImportExtensions(extensionProbe, warnings);
    (void)extensionSupport;

    auto preparedAssets = prepareItAssets(module, options, warnings);
    if (!preparedAssets.has_value()) {
        return std::unexpected(preparedAssets.error());
    }
    const auto& convertedSamples = preparedAssets->convertedSamples;
    const int instrumentLimit = preparedAssets->instrumentLimit;

    std::vector<int> orderPatterns = collectOrderedPatternReferences(module, warnings);
    std::unordered_set<int> uniquePatterns(orderPatterns.begin(), orderPatterns.end());

    int estimatedTrackCount = 0;
    int sequenceSpeed = std::max(module.initialSpeed, 1);
    for (const int patternIndex : orderPatterns) {
        const ItPattern& pattern = module.patterns[static_cast<size_t>(patternIndex)];
        if (patternHasIgnoredChannels(pattern)) {
            warnings.add(std::format("Pattern {:02X} uses channels above 8; extra channels are ignored", patternIndex));
        }
        const RowTiming timing = computeRowTiming(pattern, sequenceSpeed);
        sequenceSpeed = timing.endingSpeed;
        for (int channel = 0; channel < kMaxNspcChannels; ++channel) {
            if (channelHasAnyData(pattern, channel, timing.breakRow)) {
                ++estimatedTrackCount;
            }
        }
    }

    ItImportPreview preview{};
    preview.moduleName = module.name;
    preview.orderCount = static_cast<int>(module.orders.size());
    preview.referencedPatternCount = static_cast<int>(uniquePatterns.size());
    preview.importedPatternCount = static_cast<int>(orderPatterns.size());
    preview.importedTrackCount = estimatedTrackCount;
    preview.importedInstrumentCount = instrumentLimit;
    preview.importedSampleCount = static_cast<int>(convertedSamples.size());
    preview.currentFreeAramBytes = baseProject.aramUsage().freeBytes;
    preview.freeAramAfterDeletionBytes = estimateFreeAfterDeletion(baseProject, options);

    uint32_t estimatedSampleBytes = 0;
    preview.samples.reserve(convertedSamples.size());
    for (const auto& sample : convertedSamples) {
        estimatedSampleBytes += static_cast<uint32_t>(sample.brr.data.size());
        preview.samples.push_back(ItImportSamplePreview{
            .sampleIndex = sample.sampleIndex,
            .name = sample.name,
            .looped = sample.looped,
            .sourcePcmSampleCount = sample.sourcePcmSampleCount,
            .estimatedPcmSampleCount = sample.outputPcmSampleCount,
            .estimatedBrrBytes = static_cast<uint32_t>(sample.brr.data.size()),
            .effectiveResampleRatio = sample.effectiveResampleRatio,
        });
    }
    preview.estimatedRequiredSampleBytes = estimatedSampleBytes;
    preview.warnings = std::move(warnings.warnings);
    return preview;
}

std::expected<std::pair<NspcProject, ItImportResult>, std::string>
importItFileIntoSongSlot(const NspcProject& baseProject, const std::filesystem::path& itPath, int targetSongIndex,
                         const ItImportOptions& options) {
    if (targetSongIndex < 0 || targetSongIndex >= static_cast<int>(baseProject.songs().size())) {
        return std::unexpected("Target song index is out of range");
    }

    WarningCollector warnings;
    auto parsedModule = loadItModuleFromPath(itPath, warnings);
    if (!parsedModule.has_value()) {
        return std::unexpected(parsedModule.error());
    }
    ItModule module = std::move(*parsedModule);

    NspcProject targetProject = baseProject;
    const ExtensionSupport extensionSupport = enableImportExtensions(targetProject, warnings);

    NspcProject sourceProject = targetProject;
    sourceProject.songs().clear();
    sourceProject.instruments().clear();
    sourceProject.samples().clear();

    auto preparedAssets = prepareItAssets(module, options, warnings);
    if (!preparedAssets.has_value()) {
        return std::unexpected(preparedAssets.error());
    }
    std::vector<ConvertedSampleInfo> convertedSamples = std::move(preparedAssets->convertedSamples);

    std::vector<BrrSample> sampleData;
    sampleData.reserve(convertedSamples.size());
    for (auto& sample : convertedSamples) {
        sampleData.push_back(std::move(sample.brr));
    }
    sourceProject.samples() = std::move(sampleData);

    // Convert instruments.
    std::vector<ItInstrument> sourceInstruments = std::move(preparedAssets->instruments);
    const int instrumentLimit = preparedAssets->instrumentLimit;

    std::vector<NspcInstrument> convertedInstruments;
    convertedInstruments.reserve(instrumentLimit);
    for (int i = 0; i < instrumentLimit; ++i) {
        const ItInstrument& itInstrument = sourceInstruments[static_cast<size_t>(i)];
        int sampleId = 0;
        if (!convertedSamples.empty()) {
            const int sourceSampleCount = static_cast<int>(module.samples.size());
            const int itSampleIndex = itInstrument.sampleIndex;
            if (itSampleIndex > 0 && itSampleIndex <= sourceSampleCount) {
                sampleId = std::clamp(itSampleIndex - 1, 0, static_cast<int>(convertedSamples.size()) - 1);
            }
        }

        NspcInstrument instrument{};
        instrument.id = i;
        instrument.name = itInstrument.name;
        instrument.sampleIndex = static_cast<uint8_t>(sampleId);
        convertInstrumentAdsr(itInstrument, module.initialTempo, instrument);
        const auto [basePitch, fracPitch] =
            convertC5SpeedPitch(convertedSamples[static_cast<size_t>(sampleId)].adjustedC5Speed);
        instrument.basePitchMult = basePitch;
        instrument.fracPitchMult = fracPitch;
        instrument.originalAddr = 0;
        instrument.contentOrigin = NspcContentOrigin::UserProvided;
        convertedInstruments.push_back(std::move(instrument));
    }
    std::vector<bool> noteOffKeyOffByInstrument;
    noteOffKeyOffByInstrument.reserve(convertedInstruments.size());
    for (const auto& instrument : convertedInstruments) {
        noteOffKeyOffByInstrument.push_back((instrument.adsr1 & 0x80u) != 0u);
    }
    sourceProject.instruments() = convertedInstruments;

    // Convert sequence + patterns + tracks.
    NspcSong convertedSong{};
    convertedSong.setSongId(0);
    convertedSong.setSongName(module.name);
    convertedSong.setContentOrigin(NspcContentOrigin::UserProvided);

    std::vector<int> orderPatterns = collectOrderedPatternReferences(module, warnings);
    int nextPatternId = 0;
    int nextTrackId = 0;
    NspcEventId nextEventId = 1;
    std::array<std::unordered_map<std::string, int>, kMaxNspcChannels> dedupTrackIdByChannel;
    std::array<bool, kMaxNspcChannels> setupAnchorConsumed{};
    setupAnchorConsumed.fill(false);
    std::array<TrackState, kMaxNspcChannels> channelHistoryStates{};
    for (int channel = 0; channel < kMaxNspcChannels; ++channel) {
        TrackState state{};
        if (channel >= 0 && channel < static_cast<int>(module.initialChannelVolume.size())) {
            state.currentChannelVolume =
                std::clamp(module.initialChannelVolume[static_cast<size_t>(channel)], 0, 64);
        }
        state.lastGlobalVolume = std::clamp(module.globalVolume, 0, 128);
        state.lastTempo = std::max(module.initialTempo, 1);
        state.currentVolume =
            computeItVoiceVolume(state.currentNoteVolume, state.currentChannelVolume, module, sourceInstruments, -1);
        channelHistoryStates[static_cast<size_t>(channel)] = state;
    }
    std::vector<int> orderedSongPatternIds;
    orderedSongPatternIds.reserve(orderPatterns.size());
    int sequenceSpeed = std::max(module.initialSpeed, 1);

    for (const int itPatternIndex : orderPatterns) {

        const ItPattern& pattern = module.patterns[static_cast<size_t>(itPatternIndex)];
        if (patternHasIgnoredChannels(pattern)) {
            warnings.add(std::format("Pattern {:02X} uses channels above 8; extra channels are ignored", itPatternIndex));
        }

        const RowTiming timing = computeRowTiming(pattern, sequenceSpeed);
        sequenceSpeed = timing.endingSpeed;
        std::array<int, 8> trackIds{};
        trackIds.fill(-1);

        for (int channel = 0; channel < kMaxNspcChannels; ++channel) {
            if (!channelHasAnyData(pattern, channel, timing.breakRow)) {
                continue;
            }
            const bool isSetupAnchorTrack = !setupAnchorConsumed[static_cast<size_t>(channel)];

            TrackConversionResult conversion =
                convertPatternChannelToTrack(pattern, itPatternIndex, channel, timing, extensionSupport, module,
                                             sourceInstruments, noteOffKeyOffByInstrument, warnings, nextTrackId,
                                             nextEventId,
                                             channelHistoryStates[static_cast<size_t>(channel)]);
            NspcTrack track = std::move(conversion.track);
            channelHistoryStates[static_cast<size_t>(channel)] = conversion.endState;

            if (!trackHasNonTimingData(track)) {
                continue;
            }

            std::optional<std::string> dedupKey;
            if (!isSetupAnchorTrack) {
                auto dedupKeyResult = buildTrackDedupKey(track, sourceProject.engineConfig());
                if (dedupKeyResult.has_value()) {
                    dedupKey = std::move(*dedupKeyResult);
                    const auto existingTrackIt = dedupTrackIdByChannel[static_cast<size_t>(channel)].find(*dedupKey);
                    if (existingTrackIt != dedupTrackIdByChannel[static_cast<size_t>(channel)].end()) {
                        trackIds[static_cast<size_t>(channel)] = existingTrackIt->second;
                        continue;
                    }
                } else {
                    warnings.add(std::format("Track dedupe skipped at pattern {:02X} ch {}: {}",
                                             itPatternIndex, channel, dedupKeyResult.error()));
                }
            }

            track.id = nextTrackId;
            trackIds[static_cast<size_t>(channel)] = nextTrackId;
            convertedSong.tracks().push_back(std::move(track));
            if (!isSetupAnchorTrack && dedupKey.has_value()) {
                dedupTrackIdByChannel[static_cast<size_t>(channel)].emplace(std::move(*dedupKey), nextTrackId);
            }
            if (isSetupAnchorTrack) {
                setupAnchorConsumed[static_cast<size_t>(channel)] = true;
            }
            ++nextTrackId;
        }

        const int songPatternId = nextPatternId++;
        orderedSongPatternIds.push_back(songPatternId);
        convertedSong.patterns().push_back(NspcPattern{
            .id = songPatternId,
            .channelTrackIds = trackIds,
            .trackTableAddr = 0,
        });
    }

    injectInitialSequenceState(convertedSong, orderedSongPatternIds, module, sourceInstruments, extensionSupport,
                               nextEventId, warnings);
    for (auto& track : convertedSong.tracks()) {
        mergeClusteredVolumeVcmds(track);
    }

    for (const int songPatternId : orderedSongPatternIds) {
        convertedSong.sequence().push_back(PlayPattern{
            .patternId = songPatternId,
            .trackTableAddr = 0,
        });
    }

    std::optional<int> loopIndex = std::nullopt;
    if (module.loopOrder.has_value()) {
        if (*module.loopOrder >= 0 && *module.loopOrder < static_cast<int>(orderedSongPatternIds.size())) {
            loopIndex = *module.loopOrder;
        } else {
            warnings.add(std::format("IT loop order {} is out of range; sequence ends normally", *module.loopOrder));
        }
    }

    if (loopIndex.has_value()) {
        convertedSong.sequence().push_back(AlwaysJump{
            .opcode = 0x82,
            .target = SequenceTarget{
                .index = loopIndex,
                .addr = 0,
            },
        });
    } else {
        convertedSong.sequence().push_back(EndSequence{});
    }

    sourceProject.songs().push_back(std::move(convertedSong));
    sourceProject.refreshAramUsage();

    SongPortRequest request{};
    request.sourceSongIndex = 0;
    request.targetSongIndex = targetSongIndex;
    request.instrumentMappings = buildDefaultMappings(sourceProject, targetProject, 0);
    request.instrumentsToDelete = options.instrumentsToDelete;
    request.samplesToDelete = options.samplesToDelete;
    for (auto& mapping : request.instrumentMappings) {
        mapping.sampleAction = InstrumentMapping::SampleAction::CopyNew;
    }

    SongPortResult portResult = portSong(sourceProject, targetProject, request);
    if (!portResult.success) {
        return std::unexpected(std::format("IT import failed while porting song: {}", portResult.error));
    }
    targetProject.refreshAramUsage();

    ItImportResult result{};
    result.targetSongIndex = targetSongIndex;
    result.importedInstrumentCount = static_cast<int>(sourceProject.instruments().size());
    result.importedSampleCount = static_cast<int>(sourceProject.samples().size());
    result.importedPatternCount = static_cast<int>(sourceProject.songs().front().patterns().size());
    result.importedTrackCount = static_cast<int>(sourceProject.songs().front().tracks().size());
    result.enabledExtensions = extensionSupport.enabledNames;
    result.warnings = std::move(warnings.warnings);

    return std::pair{std::move(targetProject), std::move(result)};
}

}  // namespace ntrak::nspc
