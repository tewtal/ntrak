#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ntrak::emulation {

/// @brief DSP interpolation methods for sample playback
enum class DspInterpolation {
    Gauss,  ///< Original SNES Gaussian interpolation (authentic)
    Cubic,  ///< Cubic spline interpolation (smoother)
    Sinc,   ///< Sinc interpolation (highest quality)
    None    ///< No interpolation (raw/sharp)
};

/// @brief SPC file metadata loaded from .spc files
struct SpcFileInfo {
    std::string songTitle;
    std::string gameTitle;
    std::string dumper;
    std::string artist;
    std::string comment;
    uint32_t trackLengthSeconds = 0;
    uint32_t fadeLengthMs = 0;
};

/// @brief Per-voice DSP state for visualization/monitoring
struct VoiceState {
    int16_t volume[2];      ///< Current volume (L/R)
    uint16_t pitch;         ///< Current pitch
    uint8_t sourceNumber;   ///< BRR sample source number
    bool keyOn;             ///< Key on state
    bool keyOff;            ///< Key off state
    bool echoEnabled;       ///< Echo enabled for this voice
    bool noiseEnabled;      ///< Noise generator enabled
    bool pitchModEnabled;   ///< Pitch modulation enabled
    uint8_t envelopeLevel;  ///< Current envelope level (0-127)
};

/// @brief Snapshot of SPC I/O register state used by SPC file serialization.
struct SpcIoState {
    uint8_t testReg = 0;
    uint8_t controlReg = 0;
    uint8_t dspRegSelect = 0;
    std::array<uint8_t, 4> cpuInputRegs = {};
    std::array<uint8_t, 4> cpuOutputRegs = {};
    std::array<uint8_t, 2> ramRegs = {};
    std::array<uint8_t, 3> timerTargets = {};
    std::array<uint8_t, 3> timerOutputs = {};
};

enum class SpcAddressAccess : uint8_t {
    Execute,
    Read,
    Write,
};

struct SpcAddressAccessWatch {
    SpcAddressAccess access = SpcAddressAccess::Write;
    uint16_t address = 0;
    std::optional<uint8_t> value = std::nullopt;
    bool includeDummy = false;
};

struct SpcAddressAccessEvent {
    SpcAddressAccess access = SpcAddressAccess::Read;
    uint16_t address = 0;
    uint8_t value = 0;
    uint64_t cycle = 0;
    uint16_t pc = 0;
};

using SpcAddressAccessCallback = std::function<void(const SpcAddressAccessEvent&)>;

class AramView {
public:
    static constexpr size_t kSize = 64 * 1024;  // 64KB

    constexpr AramView(uint8_t* ptr, size_t size) : mem_(ptr, size) { assert(size == kSize); }

    [[nodiscard]] uint8_t read(uint16_t address) const noexcept { return mem_[address]; }

    [[nodiscard]] uint16_t read16(uint16_t address) const noexcept { return mem_[address] | (mem_[address + 1] << 8); }

    void write(uint16_t address, uint8_t value) noexcept { mem_[address] = value; }

    void write16(uint16_t address, uint16_t value) noexcept {
        mem_[address] = value & 0xFF;
        mem_[address + 1] = (value >> 8) & 0xFF;
    }

    [[nodiscard]] std::span<uint8_t> bytes(uint16_t start, size_t len) noexcept {
        assert(start + len <= kSize);
        return mem_.subspan(start, len);
    }

    [[nodiscard]] std::span<const uint8_t> bytes(uint16_t start, size_t len) const noexcept {
        assert(start + len <= kSize);
        return mem_.subspan(start, len);
    }

    [[nodiscard]] std::span<uint8_t> all() noexcept { return mem_; }

    [[nodiscard]] std::span<const uint8_t> all() const noexcept { return mem_; }

private:
    std::span<uint8_t> mem_;
};

/// @brief Main SPC700 + DSP emulation wrapper
///
/// This class wraps the ares-apu SPC700 + DSP core to provide a clean
/// interface for the ntrak music tracker.
class SpcDsp {
public:
    /// Constants
    static constexpr std::size_t AramSize = 0x10000;      ///< 64KB Audio RAM
    static constexpr std::size_t DspRegisterCount = 128;  ///< DSP register count
    static constexpr int SampleRate = 32000;              ///< Native sample rate
    static constexpr int VoiceCount = 8;                  ///< Number of DSP voices

    SpcDsp();
    ~SpcDsp();

    // Prevent copying (unique internal state)
    SpcDsp(const SpcDsp&) = delete;
    SpcDsp& operator=(const SpcDsp&) = delete;

    // Allow moving
    SpcDsp(SpcDsp&&) noexcept;
    SpcDsp& operator=(SpcDsp&&) noexcept;

    // ========== Lifecycle ==========

    /// @brief Reset the SPC and DSP to initial power-on state
    void reset();

    /// @brief Force SPC program counter (used to jump to engine entry after reset)
    void setPC(uint16_t pc);

    // ========== Emulation Control ==========

    /// @brief Run emulation for a specified number of SPC CPU cycles
    void runCycles(uint64_t cycles);

    /// @brief Run emulation to produce approximately the given number of audio samples
    void runForSamples(uint32_t sampleCount);

    /// @brief Run DSP only (no SPC CPU execution) to produce approximately the given number of audio samples
    void runDspOnlyForSamples(uint32_t sampleCount);

    /// @brief Execute a single SPC CPU instruction step
    void step();

    /// @brief Add an address watch (execute/read/write) callback
    uint32_t addAddressWatch(const SpcAddressAccessWatch& watch, SpcAddressAccessCallback callback);

    /// @brief Remove an address watch callback
    bool removeAddressWatch(uint32_t watchId);

    /// @brief Clear all address watches
    void clearAddressWatches();

    /// @brief Run and print a detailed per-instruction trace to stdout (not supported by ares-apu)
    void traceInstructions(uint32_t instructionCount, bool includeMemoryAccess, bool includeDummyAccess,
                           uint32_t maxMemoryAccessPerInstruction);

    /// @brief Get current SPC cycle count
    uint64_t cycleCount() const;

    // ========== Audio Output ==========

    /// @brief Get number of audio samples available in the buffer
    uint32_t sampleCount() const;

    /// @brief Get pointer to the audio sample buffer
    const int16_t* sampleBuffer() const;

    /// @brief Copy audio samples to destination buffer and clear internal buffer
    uint32_t extractSamples(int16_t* dest, uint32_t maxSamples);

    /// @brief Clear the audio sample buffer
    void clearSampleBuffer();

    // ========== ARAM Access ==========

    /// @brief Read a byte from Audio RAM
    uint8_t readAram(uint16_t address) const;

    /// @brief Write a byte to Audio RAM
    void writeAram(uint16_t address, uint8_t value);

    /// @brief Read a block of Audio RAM
    std::span<const uint8_t> readAramBlock(uint16_t address, size_t size) const;

    /// @brief Write a block to Audio RAM
    void writeAramBlock(uint16_t address, std::span<const uint8_t> data);

    /// @brief Get direct pointer to ARAM (for bulk operations)
    AramView aram();
    const AramView aram() const;

    // ========== DSP Register Access ==========

    /// @brief Read a DSP register
    uint8_t readDspRegister(uint8_t reg) const;

    /// @brief Write a DSP register
    void writeDspRegister(uint8_t reg, uint8_t value);

    // ========== SPC CPU Port Access ==========

    /// @brief Read from SPC CPU output port (as seen by main CPU)
    uint8_t readPort(uint8_t port) const;

    /// @brief Write to SPC CPU input port (as seen by main CPU)
    void writePort(uint8_t port, uint8_t value);

    /// @brief Read the live SPC I/O register state for SPC snapshot export.
    SpcIoState ioState() const;

    // ========== SPC File Loading ==========

    /// @brief Load an SPC file into the emulator
    bool loadSpcFile(const uint8_t* data, uint32_t size);

    /// @brief Load an SPC file and get its metadata
    bool loadSpcFile(const uint8_t* data, uint32_t size, SpcFileInfo& info);

    // ========== Configuration ==========

    /// @brief Set per-voice volume (0-100)
    void setVoiceVolume(uint8_t voice, int32_t volume);

    /// @brief Get per-voice volume
    int32_t voiceVolume(uint8_t voice) const;

    /// @brief Mute/unmute a specific voice
    void setVoiceMuted(uint8_t voice, bool muted);

    /// @brief Check if a voice is muted
    bool isVoiceMuted(uint8_t voice) const;

    /// @brief Set DSP interpolation method
    void setInterpolation(DspInterpolation method);

    /// @brief Get current DSP interpolation method
    DspInterpolation interpolation() const;

    // ========== State Monitoring ==========

    /// @brief Get the state of a DSP voice
    VoiceState voiceState(uint8_t voice) const;

    /// @brief Check if the SPC is running (not stopped/sleeping)
    bool isRunning() const;

    /// @brief Check if global mute is active
    bool isMuted() const;

    /// @brief Get SPC program counter
    uint16_t pc() const;

    /// @brief Get SPC accumulator
    uint8_t a() const;

    /// @brief Get SPC X register
    uint8_t x() const;

    /// @brief Get SPC Y register
    uint8_t y() const;

    /// @brief Get SPC stack pointer
    uint8_t sp() const;

    /// @brief Get SPC processor status
    uint8_t ps() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ntrak::emulation
