#include "ntrak/nspc/BrrCodec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ntrak::nspc {
namespace {

// BRR encoding logic adapted from BRRtools by Bregalad / Kode54 / Optiroc.
int brrPrediction(uint8_t filter, int p1, int p2) {
    switch (filter) {
    case 0:
        return 0;
    case 1: {
        int p = p1;
        p -= p1 >> 4;
        return p;
    }
    case 2: {
        int p = p1 << 1;
        p += (-(p1 + (p1 << 1))) >> 5;
        p -= p2;
        p += p2 >> 4;
        return p;
    }
    case 3: {
        int p = p1 << 1;
        p += (-(p1 + (p1 << 2) + (p1 << 3))) >> 6;
        p -= p2;
        p += (p2 + (p2 << 1)) >> 4;
        return p;
    }
    default:
        return 0;
    }
}

int clamp16LikeBrrtools(int value) {
    if (static_cast<int16_t>(value) != value) {
        return static_cast<int16_t>(0x7fff - (value >> 24));
    }
    return value;
}

struct EncoderState {
    int p1 = 0;
    int p2 = 0;
    uint8_t filterAtLoop = 0;
    int p1AtLoop = 0;
    int p2AtLoop = 0;
    std::array<bool, 4> filterEnabled = {true, true, true, true};
    bool wrapEnabled = true;
};

double mashBlock(EncoderState& state, uint8_t shiftAmount, uint8_t filter, const std::array<int, 16>& pcmData,
                 bool writeBlock, bool isEndPoint, std::array<uint8_t, 9>& outBlock) {
    double error = 0.0;
    int l1 = state.p1;
    int l2 = state.p2;
    const int step = 1 << shiftAmount;

    for (int i = 0; i < 16; ++i) {
        int vlin = brrPrediction(filter, l1, l2) >> 1;
        int d = (pcmData[static_cast<size_t>(i)] >> 1) - vlin;
        const int da = std::abs(d);
        if (state.wrapEnabled && da > 16384 && da < 32768) {
            d = d - 32768 * (d >> 24);
        }

        int dp = d + (step << 2) + (step >> 2);
        int c = 0;
        if (dp > 0) {
            if (step > 1) {
                c = dp / (step / 2);
            } else {
                c = dp * 2;
            }
            if (c > 15) {
                c = 15;
            }
        }
        c -= 8;
        dp = (c << shiftAmount) >> 1;
        if (shiftAmount > 12) {
            dp = (dp >> 14) & ~0x7FF;
        }
        c &= 0x0F;

        l2 = l1;
        l1 = clamp16LikeBrrtools(vlin + dp) * 2;

        const int e = pcmData[static_cast<size_t>(i)] - l1;
        error += static_cast<double>(e) * static_cast<double>(e);

        if (writeBlock) {
            outBlock[static_cast<size_t>(1 + (i >> 1))] |= static_cast<uint8_t>((i & 1) ? c : (c << 4));
        }
    }

    if (isEndPoint) {
        switch (state.filterAtLoop) {
        case 0:
            error /= 16.0;
            break;
        case 1: {
            const int e = l1 - state.p1AtLoop;
            error += static_cast<double>(e) * static_cast<double>(e);
            error /= 17.0;
            break;
        }
        default: {
            const int e1 = l1 - state.p1AtLoop;
            const int e2 = l2 - state.p2AtLoop;
            error += static_cast<double>(e1) * static_cast<double>(e1);
            error += static_cast<double>(e2) * static_cast<double>(e2);
            error /= 18.0;
            break;
        }
        }
    } else {
        error /= 16.0;
    }

    if (writeBlock) {
        state.p1 = l1;
        state.p2 = l2;
        outBlock[0] = static_cast<uint8_t>((shiftAmount << 4) | (filter << 2));
        if (isEndPoint) {
            outBlock[0] |= 0x01;
        }
    }

    return error;
}

void encodeAdpcmBlock(EncoderState& state, const std::array<int, 16>& pcmData, bool isLoopPoint, bool isEndPoint,
                      std::array<uint8_t, 9>& outBlock) {
    int bestShift = 0;
    int bestFilter = 0;
    double bestError = std::numeric_limits<double>::infinity();

    for (int shift = 0; shift < 13; ++shift) {
        for (int filter = 0; filter < 4; ++filter) {
            if (!state.filterEnabled[static_cast<size_t>(filter)]) {
                continue;
            }

            std::array<uint8_t, 9> discard{};
            const double error = mashBlock(state, static_cast<uint8_t>(shift), static_cast<uint8_t>(filter), pcmData,
                                           false, isEndPoint, discard);
            if (error < bestError) {
                bestError = error;
                bestShift = shift;
                bestFilter = filter;
            }
        }
    }

    if (isLoopPoint) {
        state.filterAtLoop = static_cast<uint8_t>(bestFilter);
        state.p1AtLoop = state.p1;
        state.p2AtLoop = state.p2;
    }

    outBlock.fill(0);
    (void)mashBlock(state, static_cast<uint8_t>(bestShift), static_cast<uint8_t>(bestFilter), pcmData, true, isEndPoint,
                    outBlock);
}

std::expected<std::vector<int>, std::string> normalizeInputPcm(std::span<const int16_t> monoPcm) {
    if (monoPcm.empty()) {
        return std::unexpected("Input PCM data is empty");
    }

    std::vector<int> samples;
    samples.reserve(monoPcm.size());
    for (const int16_t sample : monoPcm) {
        samples.push_back(static_cast<int>(sample));
    }

    if ((samples.size() % 16u) != 0u) {
        const size_t padding = 16u - (samples.size() % 16u);
        samples.insert(samples.begin(), padding, 0);
    }

    if (samples.size() < 16u) {
        samples.resize(16u, 0);
    }

    return samples;
}

int decodeNibble(int nibble, uint8_t shiftAmount, uint8_t filter, int& p1, int& p2) {
    int a = 0;
    if (shiftAmount <= 0x0C) {
        a = (((nibble < 8) ? nibble : (nibble - 16)) << shiftAmount) >> 1;
    } else {
        a = (nibble < 8) ? 2048 : -2048;
    }

    a += brrPrediction(filter, p1, p2);
    if (a > 0x7fff) {
        a = 0x7fff;
    } else if (a < -0x8000) {
        a = -0x8000;
    }
    if (a > 0x3fff) {
        a -= 0x8000;
    } else if (a < -0x4000) {
        a += 0x8000;
    }

    p2 = p1;
    p1 = a;
    return 2 * p1;
}

[[nodiscard]] std::vector<int16_t> applyTrebleBoostFilter(std::span<const int16_t> monoPcm) {
    // Tepples compensation coefficients (scaled variant used by mITroid/BRRtools frontends).
    constexpr std::array<double, 8> coefs = {
        0.912962, -0.16199, -0.0153283, 0.0426783, -0.0372004, 0.023436, -0.0105816, 0.00250474,
    };

    std::vector<int16_t> out(monoPcm.size(), 0);
    if (monoPcm.empty()) {
        return out;
    }

    for (size_t i = 0; i < monoPcm.size(); ++i) {
        double acc = static_cast<double>(monoPcm[i]) * coefs[0];
        for (size_t k = 1; k < coefs.size(); ++k) {
            const size_t plus = std::min(i + k, monoPcm.size() - 1u);
            const size_t minus = (i >= k) ? (i - k) : 0u;
            acc += coefs[k] * static_cast<double>(monoPcm[plus]);
            acc += coefs[k] * static_cast<double>(monoPcm[minus]);
        }
        out[i] = static_cast<int16_t>(std::clamp(static_cast<int>(std::lround(acc)), -32768, 32767));
    }

    return out;
}

}  // namespace

std::expected<BrrEncodeResult, std::string> encodePcm16ToBrr(std::span<const int16_t> monoPcm,
                                                             const BrrEncodeOptions& options) {
    std::vector<int16_t> trebleBoosted;
    std::span<const int16_t> workingPcm = monoPcm;
    if (options.enhanceTreble) {
        trebleBoosted = applyTrebleBoostFilter(monoPcm);
        workingPcm = std::span<const int16_t>(trebleBoosted.data(), trebleBoosted.size());
    }

    auto normalized = normalizeInputPcm(workingPcm);
    if (!normalized.has_value()) {
        return std::unexpected(normalized.error());
    }

    auto samples = std::move(*normalized);
    size_t loopStartSample = options.loopStartSample.value_or(0u);
    if (options.enableLoop) {
        if (loopStartSample >= workingPcm.size()) {
            return std::unexpected("Loop start sample is out of range");
        }
        const size_t prependedSamples = samples.size() - workingPcm.size();
        loopStartSample += prependedSamples;
        loopStartSample -= (loopStartSample % 16u);
    }

    bool addInitialBlock = false;
    for (size_t i = 0; i < 16u; ++i) {
        if (samples[i] != 0) {
            addInitialBlock = true;
            break;
        }
    }

    EncoderState state;
    std::vector<uint8_t> out;
    out.reserve((samples.size() / 16u + (addInitialBlock ? 1u : 0u)) * 9u);

    if (addInitialBlock) {
        const uint8_t loopFlag = options.enableLoop ? 0x02 : 0x00;
        out.push_back(loopFlag);
        out.insert(out.end(), 8u, 0u);
    }

    for (size_t sampleIndex = 0; sampleIndex < samples.size(); sampleIndex += 16u) {
        std::array<int, 16> blockPcm{};
        for (size_t i = 0; i < 16u; ++i) {
            blockPcm[i] = samples[sampleIndex + i];
        }

        std::array<uint8_t, 9> block{};
        const bool isLoopPoint = options.enableLoop && (sampleIndex == loopStartSample);
        const bool isEndPoint = (sampleIndex + 16u == samples.size());
        encodeAdpcmBlock(state, blockPcm, isLoopPoint, isEndPoint, block);
        if (options.enableLoop) {
            block[0] |= 0x02;
        }
        out.insert(out.end(), block.begin(), block.end());
    }

    BrrEncodeResult result;
    result.bytes = std::move(out);
    if (options.enableLoop) {
        const size_t loopBlockIndex = (loopStartSample / 16u) + (addInitialBlock ? 1u : 0u);
        result.loopOffsetBytes = static_cast<uint32_t>(loopBlockIndex * 9u);
    }
    return result;
}

std::expected<std::vector<int16_t>, std::string> decodeBrrToPcm(std::span<const uint8_t> brrData) {
    if (brrData.empty()) {
        return std::unexpected("BRR data is empty");
    }
    if ((brrData.size() % 9u) != 0u) {
        return std::unexpected("BRR data size must be a multiple of 9 bytes");
    }

    std::vector<int16_t> pcm;
    pcm.reserve((brrData.size() / 9u) * 16u);

    int p1 = 0;
    int p2 = 0;
    for (size_t blockOffset = 0; blockOffset < brrData.size(); blockOffset += 9u) {
        const uint8_t header = brrData[blockOffset];
        const uint8_t filter = static_cast<uint8_t>((header & 0x0C) >> 2);
        const uint8_t shiftAmount = static_cast<uint8_t>((header >> 4) & 0x0F);

        for (size_t i = 0; i < 8u; ++i) {
            const uint8_t byte = brrData[blockOffset + 1u + i];
            const int high = static_cast<int>((byte >> 4) & 0x0F);
            const int low = static_cast<int>(byte & 0x0F);
            pcm.push_back(static_cast<int16_t>(decodeNibble(high, shiftAmount, filter, p1, p2)));
            pcm.push_back(static_cast<int16_t>(decodeNibble(low, shiftAmount, filter, p1, p2)));
        }

        if ((header & 0x01u) != 0u) {
            break;
        }
    }

    return pcm;
}

}  // namespace ntrak::nspc
