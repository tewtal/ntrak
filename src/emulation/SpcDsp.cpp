#include "ntrak/emulation/SpcDsp.hpp"

#include "ares-apu/include/ares-apu.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ntrak::emulation {

namespace {

constexpr uint32_t kSpcMinimumSize = 0x10180;
constexpr uint32_t kSpcRamOffset = 0x100;
constexpr uint32_t kSpcRamSize = 0x10000;
constexpr uint32_t kSpcDspRegOffset = 0x10100;
constexpr uint32_t kSpcDspRegSize = 128;

constexpr uint32_t kPcOffset = 0x25;
constexpr uint32_t kAOffset = 0x27;
constexpr uint32_t kXOffset = 0x28;
constexpr uint32_t kYOffset = 0x29;
constexpr uint32_t kPsOffset = 0x2A;
constexpr uint32_t kSpOffset = 0x2B;

constexpr uint32_t kSongTitleOffset = 0x2E;
constexpr uint32_t kSongTitleSize = 0x20;
constexpr uint32_t kGameTitleOffset = 0x4E;
constexpr uint32_t kGameTitleSize = 0x20;
constexpr uint32_t kDumperOffset = 0x6E;
constexpr uint32_t kDumperSize = 0x10;
constexpr uint32_t kCommentOffset = 0x7E;
constexpr uint32_t kCommentSize = 0x20;
constexpr uint32_t kTrackLengthOffset = 0xA9;
constexpr uint32_t kTrackLengthSize = 0x03;
constexpr uint32_t kFadeLengthOffset = 0xAC;
constexpr uint32_t kFadeLengthSize = 0x05;
constexpr uint32_t kArtistOffset = 0xB1;
constexpr uint32_t kArtistSize = 0x20;

struct ParsedSpcFile {
    std::string songTitle;
    std::string gameTitle;
    std::string dumper;
    std::string artist;
    std::string comment;

    uint16_t pc = 0;
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t ps = 0;
    uint8_t sp = 0;

    std::array<uint8_t, 4> cpuRegs = {};
    uint8_t controlReg = 0;
    std::array<uint8_t, 2> ramRegs = {};
    std::array<uint8_t, 3> timerOutput = {};
    std::array<uint8_t, 3> timerTarget = {};

    uint8_t dspRegSelect = 0;
    std::array<uint8_t, 128> dspRegs = {};
    std::array<uint8_t, 0x10000> spcRam = {};

    uint32_t trackLength = 0;
    uint32_t fadeLength = 0;
};

std::string readSpcTextField(const uint8_t* data, uint32_t offset, uint32_t length) {
    const char* start = reinterpret_cast<const char*>(data + offset);
    size_t end = 0;
    while (end < length && start[end] != '\0') {
        ++end;
    }
    return std::string(start, end);
}

uint32_t readSpcDecimalField(const uint8_t* data, uint32_t offset, uint32_t length) {
    const char* start = reinterpret_cast<const char*>(data + offset);
    size_t end = 0;
    while (end < length && start[end] != '\0' && start[end] != ' ') {
        if (start[end] < '0' || start[end] > '9') {
            return 0;
        }
        ++end;
    }
    if (end == 0) {
        return 0;
    }

    uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(start, start + end, value);
    if (ec != std::errc() || ptr != start + end) {
        return 0;
    }
    return value;
}

bool parseSpcFile(const uint8_t* data, uint32_t size, ParsedSpcFile& out) {
    if (data == nullptr || size < kSpcMinimumSize) {
        return false;
    }

    constexpr std::string_view kSignature = "SNES-SPC700 Sound File Data";
    if (std::memcmp(data, kSignature.data(), kSignature.size()) != 0) {
        return false;
    }

    out.songTitle = readSpcTextField(data, kSongTitleOffset, kSongTitleSize);
    out.gameTitle = readSpcTextField(data, kGameTitleOffset, kGameTitleSize);
    out.dumper = readSpcTextField(data, kDumperOffset, kDumperSize);
    out.artist = readSpcTextField(data, kArtistOffset, kArtistSize);
    out.comment = readSpcTextField(data, kCommentOffset, kCommentSize);
    out.trackLength = readSpcDecimalField(data, kTrackLengthOffset, kTrackLengthSize);
    out.fadeLength = readSpcDecimalField(data, kFadeLengthOffset, kFadeLengthSize);

    std::memcpy(out.spcRam.data(), data + kSpcRamOffset, kSpcRamSize);
    std::memcpy(out.dspRegs.data(), data + kSpcDspRegOffset, kSpcDspRegSize);

    out.pc = static_cast<uint16_t>(data[kPcOffset] | (data[kPcOffset + 1] << 8));
    out.a = data[kAOffset];
    out.x = data[kXOffset];
    out.y = data[kYOffset];
    out.ps = data[kPsOffset];
    out.sp = data[kSpOffset];

    out.controlReg = data[kSpcRamOffset + 0xF1];
    out.dspRegSelect = data[kSpcRamOffset + 0xF2];
    out.cpuRegs = {
        data[kSpcRamOffset + 0xF4],
        data[kSpcRamOffset + 0xF5],
        data[kSpcRamOffset + 0xF6],
        data[kSpcRamOffset + 0xF7],
    };
    out.ramRegs = {
        data[kSpcRamOffset + 0xF8],
        data[kSpcRamOffset + 0xF9],
    };
    out.timerTarget = {
        data[kSpcRamOffset + 0xFA],
        data[kSpcRamOffset + 0xFB],
        data[kSpcRamOffset + 0xFC],
    };
    out.timerOutput = {
        data[kSpcRamOffset + 0xFD],
        data[kSpcRamOffset + 0xFE],
        data[kSpcRamOffset + 0xFF],
    };

    return true;
}

}  // namespace

// ============================================================================
// Implementation class (PIMPL pattern)
// ============================================================================

class SpcDsp::Impl {
public:
    struct AddressWatchEntry {
        uint32_t id = 0;
        SpcAddressAccessWatch watch;
        SpcAddressAccessCallback callback;
    };

    AresAPU apu;
    std::array<bool, 8> voiceMuted = {};
    std::array<int32_t, 8> voiceVolumes = {100, 100, 100, 100, 100, 100, 100, 100};
    DspInterpolation interpolation = DspInterpolation::Gauss;
    uint32_t nextAddressWatchId = 1;
    std::vector<AddressWatchEntry> addressWatches;

    // Sample buffer management
    std::vector<int16_t> sampleBuffer;
    constexpr static size_t kMaxSamples = 65536;  // Max stereo pairs

    // For cycle tracking
    uint64_t totalCycles = 0;

    Impl() {
        sampleBuffer.reserve(kMaxSamples * 2);
        apu.reset();  // Initialize the APU (powers on DSP and SMP)
        installMemoryAccessHook();
        UpdateChannelMask();
    }

    static void OnMemoryAccess(AresAPU::MemoryAccessType access, uint16_t address, uint8_t value, uint64_t cycle,
                               uint16_t pc, bool isDummy, void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self) {
            return;
        }
        self->NotifyAddressWatches(access, address, value, cycle, pc, isDummy);
    }

    void installMemoryAccessHook() {
        apu.setMemoryAccessHook(&Impl::OnMemoryAccess, this);
    }

    void NotifyAddressWatches(AresAPU::MemoryAccessType access, uint16_t address, uint8_t value, uint64_t cycle,
                              uint16_t pc, bool isDummy) {
        if (addressWatches.empty()) {
            return;
        }

        SpcAddressAccess mappedAccess = SpcAddressAccess::Read;
        switch (access) {
        case AresAPU::MemoryAccessType::Execute:
            mappedAccess = SpcAddressAccess::Execute;
            break;
        case AresAPU::MemoryAccessType::Read:
            mappedAccess = SpcAddressAccess::Read;
            break;
        case AresAPU::MemoryAccessType::Write:
            mappedAccess = SpcAddressAccess::Write;
            break;
        }

        const SpcAddressAccessEvent event{
            .access = mappedAccess,
            .address = address,
            .value = value,
            .cycle = cycle,
            .pc = pc,
        };

        for (const auto& watchEntry : addressWatches) {
            const auto& watch = watchEntry.watch;
            if (watch.access != mappedAccess) {
                continue;
            }
            if (!watch.includeDummy && isDummy) {
                continue;
            }
            if (watch.address != address) {
                continue;
            }
            if (watch.value.has_value() && watch.value.value() != value) {
                continue;
            }
            watchEntry.callback(event);
        }
    }

    void UpdateChannelMask() {
        uint8_t mask = 0;
        for (int i = 0; i < 8; i++) {
            if (!voiceMuted[i] && voiceVolumes[i] > 0) {
                mask |= (1 << i);
            }
        }
        apu.setChannelMask(mask);
    }

    void UpdateVoiceVolume(uint8_t voice) {
        if (voice >= 8) {
            return;
        }
        // ares-apu uses channel mask for muting, volumes are handled by DSP registers
        UpdateChannelMask();
    }

    void UpdateInterpolation() {
        // Note: ares-apu doesn't expose interpolation mode directly in the public API
        // The DSP always uses Gaussian interpolation (authentic hardware behavior)
        // This is a no-op for now, but we keep the state for API consistency
    }
};


// ============================================================================
// Constructor / Destructor
// ============================================================================

SpcDsp::SpcDsp() : impl_(std::make_unique<Impl>()) {}

SpcDsp::~SpcDsp() = default;

SpcDsp::SpcDsp(SpcDsp&&) noexcept = default;
SpcDsp& SpcDsp::operator=(SpcDsp&&) noexcept = default;

// ============================================================================
// Lifecycle
// ============================================================================

void SpcDsp::reset() {
    impl_->apu.reset();
    impl_->installMemoryAccessHook();
    impl_->totalCycles = 0;
    impl_->sampleBuffer.clear();
}

void SpcDsp::setPC(uint16_t pc) {
    impl_->apu.setPC(pc);
}

// ============================================================================
// Emulation Control
// ============================================================================

void SpcDsp::runCycles(uint64_t cycles) {
    // ares-apu steps by samples, not cycles
    uint32_t samplesToGenerate = static_cast<uint32_t>((cycles + 63) / 64);
    
    for (uint32_t i = 0; i < samplesToGenerate; i++) {
        auto sample = impl_->apu.step();
        impl_->sampleBuffer.push_back(sample.left);
        impl_->sampleBuffer.push_back(sample.right);
    }
    
    impl_->totalCycles += cycles;
}

void SpcDsp::runForSamples(uint32_t sampleCount) {
    for (uint32_t i = 0; i < sampleCount; i++) {
        auto sample = impl_->apu.step();
        impl_->sampleBuffer.push_back(sample.left);
        impl_->sampleBuffer.push_back(sample.right);
    }
    impl_->totalCycles += static_cast<uint64_t>(sampleCount) * 32;
}

void SpcDsp::runDspOnlyForSamples(uint32_t sampleCount) {
    for (uint32_t i = 0; i < sampleCount; i++) {
        auto sample = impl_->apu.stepDSPOnly();
        impl_->sampleBuffer.push_back(sample.left);
        impl_->sampleBuffer.push_back(sample.right);
    }
}

void SpcDsp::step() {
    // Execute one sample worth of cycles (~32 cycles)
    runCycles(32);
}

uint32_t SpcDsp::addAddressWatch(const SpcAddressAccessWatch& watch, SpcAddressAccessCallback callback) {
    if (!callback) {
        return 0;
    }

    const uint32_t id = impl_->nextAddressWatchId++;
    impl_->addressWatches.push_back(Impl::AddressWatchEntry{.id = id, .watch = watch, .callback = std::move(callback)});
    return id;
}

bool SpcDsp::removeAddressWatch(uint32_t watchId) {
    const auto it = std::remove_if(impl_->addressWatches.begin(), impl_->addressWatches.end(),
                                   [watchId](const Impl::AddressWatchEntry& entry) { return entry.id == watchId; });
    if (it == impl_->addressWatches.end()) {
        return false;
    }
    impl_->addressWatches.erase(it, impl_->addressWatches.end());
    return true;
}

void SpcDsp::clearAddressWatches() {
    impl_->addressWatches.clear();
}

void SpcDsp::traceInstructions(uint32_t instructionCount, bool includeMemoryAccess, bool includeDummyAccess,
                               uint32_t maxMemoryAccessPerInstruction) {
    // Not supported by ares-apu public API
}

uint64_t SpcDsp::cycleCount() const {
    return impl_->totalCycles;
}

// ============================================================================
// Audio Output
// ============================================================================

uint32_t SpcDsp::sampleCount() const {
    // Return stereo pair count
    return static_cast<uint32_t>(impl_->sampleBuffer.size()) / 2;
}

const int16_t* SpcDsp::sampleBuffer() const {
    return impl_->sampleBuffer.data();
}

uint32_t SpcDsp::extractSamples(int16_t* dest, uint32_t maxSamples) {
    uint32_t available = sampleCount();
    uint32_t toCopy = std::min(available, maxSamples);

    if (toCopy > 0) {
        std::memcpy(dest, impl_->sampleBuffer.data(), toCopy * 2 * sizeof(int16_t));
        // Remove copied samples from buffer
        impl_->sampleBuffer.erase(impl_->sampleBuffer.begin(), 
                                  impl_->sampleBuffer.begin() + toCopy * 2);
    }

    return toCopy;
}

void SpcDsp::clearSampleBuffer() {
    impl_->sampleBuffer.clear();
}

// ============================================================================
// ARAM Access
// ============================================================================

uint8_t SpcDsp::readAram(uint16_t address) const {
    return impl_->apu.ram()[address];
}

void SpcDsp::writeAram(uint16_t address, uint8_t value) {
    impl_->apu.ram()[address] = value;
}

std::span<const uint8_t> SpcDsp::readAramBlock(uint16_t address, size_t size) const {
    return aram().bytes(address, size);
}

void SpcDsp::writeAramBlock(uint16_t address, std::span<const uint8_t> data) {
    auto aramView = aram();
    for (size_t i = 0; i < data.size(); i++) {
        aramView.write((address + i) & 0xFFFF, data[i]);
    }
}

AramView SpcDsp::aram() {
    return AramView(impl_->apu.ram(), SpcDsp::AramSize);
}

const AramView SpcDsp::aram() const {
    return AramView(const_cast<uint8_t*>(impl_->apu.ram()), SpcDsp::AramSize);
}

// ============================================================================
// DSP Register Access
// ============================================================================

uint8_t SpcDsp::readDspRegister(uint8_t reg) const {
    return impl_->apu.readDSP(reg & 0x7F);
}

void SpcDsp::writeDspRegister(uint8_t reg, uint8_t value) {
    impl_->apu.writeDSP(reg & 0x7F, value);
}

// ============================================================================
// SPC CPU Port Access
// ============================================================================

uint8_t SpcDsp::readPort(uint8_t port) const {
    return impl_->apu.readPort(port & 0x03);
}

void SpcDsp::writePort(uint8_t port, uint8_t value) {
    impl_->apu.writePort(port & 0x03, value);
}

SpcIoState SpcDsp::ioState() const {
    // ares-apu doesn't expose all internal I/O state directly
    // We'll return a partially-filled structure with what we can access
    SpcIoState io{};
    
    // Read I/O ports from RAM locations $F0-$F9
    uint8_t* ram = impl_->apu.ram();
    
    // Most I/O registers are at $00F0-$00F9 in APU RAM
    io.testReg = ram[0xF0];      // Test register
    io.controlReg = ram[0xF1];   // Control register
    io.dspRegSelect = ram[0xF2]; // DSP register select
    
    io.cpuInputRegs = {ram[0xF4], ram[0xF5], ram[0xF6], ram[0xF7]};
    io.cpuOutputRegs = {ram[0xF4], ram[0xF5], ram[0xF6], ram[0xF7]}; // Same locations
    io.ramRegs = {ram[0xF8], ram[0xF9]};
    io.timerTargets = {ram[0xFA], ram[0xFB], ram[0xFC]};
    
    // Timer outputs are internal state, not directly accessible
    io.timerOutputs = {0, 0, 0};
    
    return io;
}

// ============================================================================
// SPC File Loading
// ============================================================================

bool SpcDsp::loadSpcFile(const uint8_t* data, uint32_t size) {
    SpcFileInfo info;
    return loadSpcFile(data, size, info);
}

bool SpcDsp::loadSpcFile(const uint8_t* data, uint32_t size, SpcFileInfo& info) {
    ParsedSpcFile spcData;
    if (!parseSpcFile(data, size, spcData)) {
        return false;
    }

    // Initialize SMP/DSP hardware state with reset (this sets PC to IPL ROM, then we restore snapshot registers)
    impl_->apu.reset(nullptr, false);  // Clear everything including RAM
    impl_->installMemoryAccessHook();
    impl_->totalCycles = 0;
    impl_->sampleBuffer.clear();
    
    // Now load SPC RAM (overwrites the cleared RAM from reset)
    uint8_t* ram = impl_->apu.ram();
    std::memcpy(ram, spcData.spcRam.data(), spcData.spcRam.size());

    // Load DSP registers
    for (size_t i = 0; i < spcData.dspRegs.size(); ++i) {
        impl_->apu.writeDSP(static_cast<uint8_t>(i), spcData.dspRegs[i]);
    }
    
    // Clear mute and reset flags to ensure audio plays
    uint8_t flg = impl_->apu.readDSP(0x6C);
    flg &= ~0xC0;  // Clear bits 6 (mute) and 7 (reset)
    impl_->apu.writeDSP(0x6C, flg);    
   
    // Disable IPL ROM BEFORE setting CPU state
    impl_->apu.writeSMPIO(0x1, spcData.controlReg & 0x7F); // Ensure IPL ROM disabled (bit 7=0)
    impl_->apu.writeSMPIO(0x2, spcData.dspRegSelect);
    impl_->apu.writeSMPIO(0xA, spcData.timerTarget[0]);
    impl_->apu.writeSMPIO(0xB, spcData.timerTarget[1]);
    impl_->apu.writeSMPIO(0xC, spcData.timerTarget[2]);
    
    // Finally set CPU registers (overwrites PC that was set by reset to IPL ROM)
    impl_->apu.setA(spcData.a);
    impl_->apu.setX(spcData.x);
    impl_->apu.setY(spcData.y);
    impl_->apu.setSP(spcData.sp);
    impl_->apu.setPS(spcData.ps);
    impl_->apu.setPC(spcData.pc);
    
    // Also write to RAM for completeness (some SPC code may read from RAM)
    //ram[0xF0] = 0x2A; // TEST register: externalWaitStates=2, timersEnable=1, ramWritable=1
    ram[0xF1] = spcData.controlReg & 0x7F;
    ram[0xF2] = spcData.dspRegSelect;
    ram[0xF4] = spcData.cpuRegs[0];
    ram[0xF5] = spcData.cpuRegs[1];
    ram[0xF6] = spcData.cpuRegs[2];
    ram[0xF7] = spcData.cpuRegs[3];
    ram[0xF8] = spcData.ramRegs[0];
    ram[0xF9] = spcData.ramRegs[1];
    ram[0xFA] = spcData.timerTarget[0];
    ram[0xFB] = spcData.timerTarget[1];
    ram[0xFC] = spcData.timerTarget[2];
    ram[0xFD] = spcData.timerOutput[0];
    ram[0xFE] = spcData.timerOutput[1];
    ram[0xFF] = spcData.timerOutput[2];

    // Extract metadata
    info.songTitle = spcData.songTitle;
    info.gameTitle = spcData.gameTitle;
    info.dumper = spcData.dumper;
    info.artist = spcData.artist;
    info.comment = spcData.comment;
    info.trackLengthSeconds = spcData.trackLength;
    info.fadeLengthMs = spcData.fadeLength;

    return true;
}

// ============================================================================
// Configuration
// ============================================================================

void SpcDsp::setVoiceVolume(uint8_t voice, int32_t volume) {
    if (voice >= 8) {
        return;
    }
    impl_->voiceVolumes[voice] = std::clamp(volume, 0, 100);
    impl_->UpdateVoiceVolume(voice);
}

int32_t SpcDsp::voiceVolume(uint8_t voice) const {
    if (voice >= 8) {
        return 0;
    }
    return impl_->voiceVolumes[voice];
}

void SpcDsp::setVoiceMuted(uint8_t voice, bool muted) {
    if (voice >= 8) {
        return;
    }
    impl_->voiceMuted[voice] = muted;
    impl_->UpdateVoiceVolume(voice);
}

bool SpcDsp::isVoiceMuted(uint8_t voice) const {
    if (voice >= 8) {
        return true;
    }
    return impl_->voiceMuted[voice];
}

void SpcDsp::setInterpolation(DspInterpolation method) {
    impl_->interpolation = method;
    impl_->UpdateInterpolation();
}

DspInterpolation SpcDsp::interpolation() const {
    return impl_->interpolation;
}

// ============================================================================
// State Monitoring
// ============================================================================

VoiceState SpcDsp::voiceState(uint8_t voice) const {
    VoiceState state = {};
    if (voice >= 8) {
        return state;
    }

    uint8_t voiceBase = voice * 0x10;

    // Read DSP registers for this voice
    state.volume[0] = static_cast<int8_t>(readDspRegister(voiceBase + 0));  // VOLL
    state.volume[1] = static_cast<int8_t>(readDspRegister(voiceBase + 1));  // VOLR
    state.pitch = readDspRegister(voiceBase + 2) | ((readDspRegister(voiceBase + 3) & 0x3F) << 8);
    state.sourceNumber = readDspRegister(voiceBase + 4);   // SRCN
    state.envelopeLevel = readDspRegister(voiceBase + 8);  // ENVX

    // Read global voice control registers
    uint8_t voiceBit = 1 << voice;
    uint8_t keyOn = readDspRegister(0x4C);    // KON
    uint8_t keyOff = readDspRegister(0x5C);   // KOF
    uint8_t echoOn = readDspRegister(0x4D);   // EON
    uint8_t noiseOn = readDspRegister(0x3D);  // NON
    uint8_t pitchMod = readDspRegister(0x2D); // PMON

    state.keyOn = (keyOn & voiceBit) != 0;
    state.keyOff = (keyOff & voiceBit) != 0;
    state.echoEnabled = (echoOn & voiceBit) != 0;
    state.noiseEnabled = (noiseOn & voiceBit) != 0;
    state.pitchModEnabled = (pitchMod & voiceBit) != 0;

    return state;
}

bool SpcDsp::isRunning() const {
    // ares-apu doesn't expose stop/wait states directly
    // Assume running unless we implement additional state tracking
    return true;
}

bool SpcDsp::isMuted() const {
    return impl_->apu.muted();
}

uint16_t SpcDsp::pc() const {
    return impl_->apu.getPC();
}

uint8_t SpcDsp::a() const {
    return impl_->apu.getA();
}

uint8_t SpcDsp::x() const {
    return impl_->apu.getX();
}

uint8_t SpcDsp::y() const {
    return impl_->apu.getY();
}

uint8_t SpcDsp::sp() const {
    return impl_->apu.getSP();
}

uint8_t SpcDsp::ps() const {
    return impl_->apu.getPS();
}

}  // namespace ntrak::emulation
