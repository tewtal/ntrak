#include "ntrak/audio/SpcPlayer.hpp"

#include "ntrak/emulation/SpcDsp.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

namespace ntrak::audio {

// DSP register offsets for voices
namespace dsp_reg {
constexpr uint8_t VxVOLL(uint8_t v) {
    return (v << 4) | 0x00;
}

constexpr uint8_t VxVOLR(uint8_t v) {
    return (v << 4) | 0x01;
}

constexpr uint8_t VxPITCHL(uint8_t v) {
    return (v << 4) | 0x02;
}

constexpr uint8_t VxPITCHH(uint8_t v) {
    return (v << 4) | 0x03;
}

constexpr uint8_t VxSRCN(uint8_t v) {
    return (v << 4) | 0x04;
}

constexpr uint8_t VxADSR1(uint8_t v) {
    return (v << 4) | 0x05;
}

constexpr uint8_t VxADSR2(uint8_t v) {
    return (v << 4) | 0x06;
}

constexpr uint8_t VxGAIN(uint8_t v) {
    return (v << 4) | 0x07;
}

constexpr uint8_t EFB = 0x0D;    // Echo feedback volume
constexpr uint8_t MVOLL = 0x0C;  // Master volume left
constexpr uint8_t MVOLR = 0x1C;  // Master volume right
constexpr uint8_t EVOLL = 0x2C;  // Echo volume left
constexpr uint8_t EVOLR = 0x3C;  // Echo volume right
constexpr uint8_t PMON = 0x2D;   // Pitch modulation enable
constexpr uint8_t NON = 0x3D;    // Noise enable
constexpr uint8_t EON = 0x4D;    // Echo enable
constexpr uint8_t KON = 0x4C;   // Key On
constexpr uint8_t KOFF = 0x5C;  // Key Off
constexpr uint8_t FLG = 0x6C;   // Flags
}  // namespace dsp_reg

SpcPlayer::SpcPlayer(AudioEngine& audioEngine)
    : audioEngine_(audioEngine), spc_(std::make_unique<emulation::SpcDsp>()),
      fileInfo_(std::make_unique<emulation::SpcFileInfo>()) {
    sampleBuffer_.resize(8192);
}

SpcPlayer::~SpcPlayer() {
    stop();
}

bool SpcPlayer::loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return false;
    }

    return loadFromMemory(buffer.data(), static_cast<uint32_t>(buffer.size()));
}

bool SpcPlayer::loadFromMemory(const uint8_t* data, uint32_t size) {
    // Stop callback first to avoid lock inversion with audio thread.
    audioEngine_.clearAudioCallback();
    std::lock_guard<std::mutex> lock(mutex_);

    // Reset runtime playback/preview state before loading a new image.
    playing_ = false;
    previewVoiceMask_ = 0;
    previewActive_ = false;
    sampleBufferPos_ = 0;
    resamplePos_ = 0.0;
    spc_->clearSampleBuffer();

    spc_->reset();
    if (!spc_->loadSpcFile(data, size, *fileInfo_)) {
        loaded_ = false;
        return false;
    }

    // Reset per-channel muting whenever a new image is loaded.
    for (uint8_t voice = 0; voice < 8; ++voice) {
        spc_->setVoiceMuted(voice, false);
    }

    loaded_ = true;
    sampleBufferPos_ = 0;
    resamplePos_ = 0.0;

    return true;
}

void SpcPlayer::play() {
    bool startCallback = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!loaded_ || playing_) {
            return;
        }

        // Start transport from an empty host-side queue so no stale mixer data leaks in.
        previewVoiceMask_ = 0;
        previewActive_ = false;
        sampleBufferPos_ = 0;
        resamplePos_ = 0.0;
        spc_->clearSampleBuffer();

        playing_ = true;
        startCallback = true;
    }

    if (startCallback) {
        audioEngine_.setAudioCallback([this](float* output, uint32_t frameCount) { audioCallback(output, frameCount); });
    }
}

void SpcPlayer::stop() {
    // Stop callback first to avoid lock inversion with audio thread.
    audioEngine_.clearAudioCallback();
    std::lock_guard<std::mutex> lock(mutex_);
    playing_ = false;
    previewVoiceMask_ = 0;
    previewActive_ = false;
    sampleBufferPos_ = 0;
    resamplePos_ = 0.0;
    spc_->clearSampleBuffer();
}

// ========== Note Preview Implementation ==========

void SpcPlayer::noteOn(const NotePreviewParams& params) {
    bool startCallback = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!playing_) {
            // Start preview from a clean output buffer so stale song audio does not leak in.
            sampleBufferPos_ = 0;
            resamplePos_ = 0.0;
            spc_->clearSampleBuffer();
        }

        uint8_t v = params.voice & 0x07;
        uint8_t vMask = static_cast<uint8_t>(1u << v);

        if (!playing_) {
            // Preview-only mode does not execute SPC CPU code, so enforce audible DSP state directly.
            // Clear reset/mute, disable echo writes, and wipe inherited modulation/echo routing.
            spc_->writeDspRegister(dsp_reg::FLG, 0x20);

            spc_->writeDspRegister(dsp_reg::MVOLL, 0x7F);
            spc_->writeDspRegister(dsp_reg::MVOLR, 0x7F);

            spc_->writeDspRegister(dsp_reg::EVOLL, 0x00);
            spc_->writeDspRegister(dsp_reg::EVOLR, 0x00);
            spc_->writeDspRegister(dsp_reg::EFB, 0x00);
            spc_->writeDspRegister(dsp_reg::PMON, 0x00);
            spc_->writeDspRegister(dsp_reg::NON, 0x00);
            spc_->writeDspRegister(dsp_reg::EON, 0x00);

            // Fully silence any stale voices, then allow the target preview voice.
            spc_->writeDspRegister(dsp_reg::KOFF, 0xFF);
            spc_->writeDspRegister(dsp_reg::KOFF, static_cast<uint8_t>(0xFFu & ~vMask));
        }

        // Set up voice registers
        spc_->writeDspRegister(dsp_reg::VxVOLL(v), static_cast<uint8_t>(params.volumeL));
        spc_->writeDspRegister(dsp_reg::VxVOLR(v), static_cast<uint8_t>(params.volumeR));
        spc_->writeDspRegister(dsp_reg::VxPITCHL(v), params.pitch & 0xFF);
        spc_->writeDspRegister(dsp_reg::VxPITCHH(v), (params.pitch >> 8) & 0x3F);
        spc_->writeDspRegister(dsp_reg::VxSRCN(v), params.sampleIndex);
        spc_->writeDspRegister(dsp_reg::VxADSR1(v), params.adsr1);
        spc_->writeDspRegister(dsp_reg::VxADSR2(v), params.adsr2);
        spc_->writeDspRegister(dsp_reg::VxGAIN(v), params.gain);

        // Trigger key on for this voice
        spc_->writeDspRegister(dsp_reg::KON, vMask);

        previewVoiceMask_ |= vMask;
        previewActive_ = previewVoiceMask_ != 0;

        // If not already playing, start the audio callback for preview.
        if (!playing_ && previewActive_) {
            startCallback = true;
        }
    }

    if (startCallback) {
        audioEngine_.setAudioCallback(
            [this](float* output, uint32_t frameCount) { audioCallback(output, frameCount); });
    }
}

void SpcPlayer::noteOff(uint8_t voice) {
    bool stopCallback = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        uint8_t v = voice & 0x07;

        // Trigger key off for this voice
        spc_->writeDspRegister(dsp_reg::KOFF, 1 << v);

        previewVoiceMask_ &= ~(1 << v);
        previewActive_ = previewVoiceMask_ != 0;

        // If no more preview notes and not playing SPC, stop callback.
        if (!playing_ && !previewActive_) {
            sampleBufferPos_ = 0;
            resamplePos_ = 0.0;
            spc_->clearSampleBuffer();
            stopCallback = true;
        }
    }

    if (stopCallback) {
        audioEngine_.clearAudioCallback();
    }
}

void SpcPlayer::allNotesOff() {
    bool stopCallback = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Key off all voices
        spc_->writeDspRegister(dsp_reg::KOFF, 0xFF);

        previewVoiceMask_ = 0;
        previewActive_ = false;

        // If not playing SPC, stop callback.
        if (!playing_) {
            sampleBufferPos_ = 0;
            resamplePos_ = 0.0;
            spc_->clearSampleBuffer();
            stopCallback = true;
        }
    }

    if (stopCallback) {
        audioEngine_.clearAudioCallback();
    }
}

// ========== DSP Access ==========

emulation::SpcDsp& SpcPlayer::spcDsp() {
    return *spc_;
}

const emulation::SpcDsp& SpcPlayer::spcDsp() const {
    return *spc_;
}

const emulation::SpcFileInfo* SpcPlayer::fileInfo() const {
    return fileInfo_.get();
}

void SpcPlayer::setChannelMask(uint8_t mask) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint8_t voice = 0; voice < 8; ++voice) {
        const bool enabled = (mask & (1u << voice)) != 0u;
        spc_->setVoiceMuted(voice, !enabled);
    }
}

// ========== Audio Processing ==========

float SpcPlayer::cubicInterpolate(float y0, float y1, float y2, float y3, float t) {
    // Catmull-Rom spline interpolation
    // Provides smooth interpolation using 4 points
    float t2 = t * t;
    float t3 = t2 * t;

    float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float a2 = -0.5f * y0 + 0.5f * y2;
    float a3 = y1;

    return a0 * t3 + a1 * t2 + a2 * t + a3;
}

void SpcPlayer::generateSamples(uint32_t count) {
    if (playing_) {
        spc_->runForSamples(count);
    } else if (previewActive_) {
        spc_->runDspOnlyForSamples(count);
    } else {
        return;
    }

    uint32_t sampleCount = spc_->sampleCount();

    if (sampleCount > 0) {
        const int16_t* samples = spc_->sampleBuffer();

        size_t neededSize = (sampleBufferPos_ + sampleCount) * 2;
        if (sampleBuffer_.size() < neededSize) {
            sampleBuffer_.resize(neededSize + 4096);
        }

        std::memcpy(sampleBuffer_.data() + sampleBufferPos_ * 2, samples, sampleCount * 2 * sizeof(int16_t));
        sampleBufferPos_ += sampleCount;
        spc_->clearSampleBuffer();
    }
}

void SpcPlayer::audioCallback(float* output, uint32_t frameCount) {
    if (!playing_ && !previewActive_) {
        std::memset(output, 0, frameCount * 2 * sizeof(float));
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const double outputSampleRate = static_cast<double>(audioEngine_.sampleRate());
    const double ratio = kSpcSampleRate / outputSampleRate;  // ~0.667 for 32kHz -> 48kHz

    for (uint32_t i = 0; i < frameCount; ++i) {
        size_t idx = static_cast<size_t>(resamplePos_);

        // Need more samples from SPC?
        // For cubic interpolation we need idx-1, idx, idx+1, idx+2 (4 samples)
        while (idx + 3 >= sampleBufferPos_) {
            generateSamples(512);
            if (spc_->sampleCount() == 0) {
                break;  // Avoid infinite loop if no samples produced
            }
        }

        // Cubic interpolation using Catmull-Rom splines
        idx = static_cast<size_t>(resamplePos_);
        float t = static_cast<float>(resamplePos_ - static_cast<double>(idx));

        if (idx >= 1 && idx + 2 < sampleBufferPos_) {
            // Left channel
            float l0 = sampleBuffer_[(idx - 1) * 2] / 32768.0f;
            float l1 = sampleBuffer_[idx * 2] / 32768.0f;
            float l2 = sampleBuffer_[(idx + 1) * 2] / 32768.0f;
            float l3 = sampleBuffer_[(idx + 2) * 2] / 32768.0f;
            output[i * 2] = cubicInterpolate(l0, l1, l2, l3, t);

            // Right channel
            float r0 = sampleBuffer_[(idx - 1) * 2 + 1] / 32768.0f;
            float r1 = sampleBuffer_[idx * 2 + 1] / 32768.0f;
            float r2 = sampleBuffer_[(idx + 1) * 2 + 1] / 32768.0f;
            float r3 = sampleBuffer_[(idx + 2) * 2 + 1] / 32768.0f;
            output[i * 2 + 1] = cubicInterpolate(r0, r1, r2, r3, t);
        } else if (idx + 1 < sampleBufferPos_) {
            // Fallback to linear at buffer edges
            float frac = t;
            output[i * 2] = (sampleBuffer_[idx * 2] / 32768.0f) * (1.0f - frac) +
                            (sampleBuffer_[(idx + 1) * 2] / 32768.0f) * frac;
            output[i * 2 + 1] = (sampleBuffer_[idx * 2 + 1] / 32768.0f) * (1.0f - frac) +
                                (sampleBuffer_[(idx + 1) * 2 + 1] / 32768.0f) * frac;
        } else {
            output[i * 2] = 0.0f;
            output[i * 2 + 1] = 0.0f;
        }

        resamplePos_ += ratio;

        // Compact buffer periodically to prevent unbounded growth
        if (resamplePos_ > 4096) {
            size_t consumedSamples = static_cast<size_t>(resamplePos_) - 1;  // Keep one extra for interpolation
            if (consumedSamples > 0 && consumedSamples < sampleBufferPos_) {
                size_t remaining = sampleBufferPos_ - consumedSamples;
                std::memmove(sampleBuffer_.data(), sampleBuffer_.data() + consumedSamples * 2,
                             remaining * 2 * sizeof(int16_t));
                sampleBufferPos_ = remaining;
                resamplePos_ -= static_cast<double>(consumedSamples);
            }
        }
    }
}

}  // namespace ntrak::audio
