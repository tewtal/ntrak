#pragma once

#include "ntrak/audio/AudioEngine.hpp"
#include "ntrak/emulation/SpcDsp.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ntrak::audio {

/// Parameters for playing a single note/sample preview
struct NotePreviewParams {
    uint8_t sampleIndex = 0;  ///< BRR sample index (0-255, from sample directory)
    uint16_t pitch = 0x1000;  ///< Pitch (0x1000 = C-4 base pitch)
    int8_t volumeL = 127;     ///< Left volume (-128 to 127)
    int8_t volumeR = 127;     ///< Right volume (-128 to 127)
    uint8_t adsr1 = 0xFF;     ///< ADSR1 register (bit 7=1 for ADSR mode)
    uint8_t adsr2 = 0xE0;     ///< ADSR2 register (sustain level + sustain rate)
    uint8_t gain = 0x7F;      ///< GAIN register (used when ADSR1 bit 7=0)
    uint8_t voice = 0;        ///< Voice to use for preview (0-7)

    /// @brief Calculate pitch for a MIDI note number
    /// @param midiNote MIDI note (60 = C-4)
    /// @param basePitch Base pitch value for the sample (default 0x1000 = C-4)
    /// @return Pitch value for DSP
    static uint16_t pitchFromMidi(int midiNote, uint16_t basePitch = 0x1000) {
        double ratio = std::pow(2.0, (midiNote - 60) / 12.0);
        return static_cast<uint16_t>(basePitch * ratio);
    }

    /// @brief Calculate DSP pitch using the N-SPC note table + instrument pitch multiplier.
    /// @param note N-SPC note number (0..0x7F)
    /// @param instrumentPitchMultiplier Instrument pitch multiplier (0x0100 = neutral)
    /// @return DSP pitch value written to VxPITCH (clamped 1..0x3FFF)
    static uint16_t pitchFromNspcNote(int note, uint16_t instrumentPitchMultiplier = 0x0100) {
        static constexpr std::array<uint16_t, 12> kPitchTable = {
            0x085F, 0x08DE, 0x0965, 0x09F4, 0x0A8C, 0x0B2C, 0x0BD6, 0x0C8B, 0x0D4A, 0x0E14, 0x0EEA, 0x0FCD,
        };

        const int clampedNote = std::clamp(note, 0, 0x7F);
        const int octave = clampedNote / 12;
        const int key = clampedNote % 12;

        uint32_t basePitch = static_cast<uint32_t>(kPitchTable[static_cast<size_t>(key)]) * 2u;
        if (octave < 6) {
            basePitch >>= static_cast<uint32_t>(6 - octave);
        } else if (octave > 6) {
            basePitch <<= static_cast<uint32_t>(octave - 6);
        }

        const uint16_t effectiveMultiplier = (instrumentPitchMultiplier == 0) ? 0x0100 : instrumentPitchMultiplier;
        const uint32_t scaled = (basePitch * static_cast<uint32_t>(effectiveMultiplier)) >> 8u;
        return static_cast<uint16_t>(std::clamp<uint32_t>(scaled, 1u, 0x3FFFu));
    }
};

/// Handles SPC file playback with high-quality resampling
/// Also supports note preview for tracker editing
class SpcPlayer {
public:
    explicit SpcPlayer(AudioEngine& audioEngine);
    ~SpcPlayer();

    SpcPlayer(const SpcPlayer&) = delete;
    SpcPlayer& operator=(const SpcPlayer&) = delete;

    /// Load an SPC file from disk
    bool loadFile(const std::string& path);

    /// Load an SPC file from memory
    bool loadFromMemory(const uint8_t* data, uint32_t size);

    /// Start playback
    void play();

    /// Stop playback
    void stop();

    /// Check if currently playing
    bool isPlaying() const { return playing_; }

    /// Check if a file is loaded
    bool isLoaded() const { return loaded_; }

    // ========== Note Preview API ==========

    /// @brief Start playing a note preview on the specified voice
    void noteOn(const NotePreviewParams& params);

    /// @brief Stop the note preview on the specified voice
    void noteOff(uint8_t voice);

    /// @brief Stop all note previews
    void allNotesOff();

    /// @brief Check if preview mode is active (any preview notes playing)
    bool isPreviewActive() const { return previewActive_; }

    // ========== Direct DSP Access ==========

    /// @brief Get direct access to the SPC/DSP emulator
    emulation::SpcDsp& spcDsp();
    const emulation::SpcDsp& spcDsp() const;

    /// @brief Get file info (valid after successful load)
    const emulation::SpcFileInfo* fileInfo() const;

    /// @brief Set per-channel playback mask (bit N = enabled, 0 = muted)
    void setChannelMask(uint8_t mask);

private:
    void audioCallback(float* output, uint32_t frameCount);
    void generateSamples(uint32_t count);

    /// Cubic (Catmull-Rom) interpolation for high-quality resampling
    static float cubicInterpolate(float y0, float y1, float y2, float y3, float t);

    AudioEngine& audioEngine_;
    std::unique_ptr<emulation::SpcDsp> spc_;
    std::unique_ptr<emulation::SpcFileInfo> fileInfo_;

    std::mutex mutex_;
    std::atomic<bool> playing_{false};
    std::atomic<bool> previewActive_{false};
    bool loaded_ = false;

    // Track which voices are used for preview (bitmask)
    uint8_t previewVoiceMask_ = 0;

    // Resampling state
    std::vector<int16_t> sampleBuffer_;
    size_t sampleBufferPos_ = 0;
    double resamplePos_ = 0.0;

    static constexpr double kSpcSampleRate = 32000.0;
};

}  // namespace ntrak::audio
