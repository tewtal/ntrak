#pragma once

#include "ntrak/emulation/SpcDsp.hpp"
#include "ntrak/nspc/NspcEngine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ntrak::nspc {

enum class NspcContentOrigin : uint8_t {
    EngineProvided,
    UserProvided,
};

struct Duration {
    uint8_t ticks;
    std::optional<uint8_t> quantization;
    std::optional<uint8_t> velocity;
};

// VCMDS
struct VcmdInst {
    static constexpr std::string_view name = "Ins";
    static constexpr uint8_t id = 0xE0;
    uint8_t instrumentIndex;
};

struct VcmdPanning {
    static constexpr std::string_view name = "Pan";
    static constexpr uint8_t id = 0xE1;
    uint8_t panning;
};

struct VcmdPanFade {
    static constexpr std::string_view name = "PFa";
    static constexpr uint8_t id = 0xE2;
    uint8_t time;
    uint8_t target;
};

struct VcmdVibratoOn {
    static constexpr std::string_view name = "VOn";
    static constexpr uint8_t id = 0xE3;
    uint8_t delay;
    uint8_t rate;
    uint8_t depth;
};

struct VcmdVibratoOff {
    static constexpr std::string_view name = "VOf";
    static constexpr uint8_t id = 0xE4;
};

struct VcmdGlobalVolume {
    static constexpr std::string_view name = "GVl";
    static constexpr uint8_t id = 0xE5;
    uint8_t volume;
};

struct VcmdGlobalVolumeFade {
    static constexpr std::string_view name = "GVF";
    static constexpr uint8_t id = 0xE6;
    uint8_t time;
    uint8_t target;
};

struct VcmdTempo {
    static constexpr std::string_view name = "Tmp";
    static constexpr uint8_t id = 0xE7;
    uint8_t tempo;
};

struct VcmdTempoFade {
    static constexpr std::string_view name = "TmF";
    static constexpr uint8_t id = 0xE8;
    uint8_t time;
    uint8_t target;
};

struct VcmdGlobalTranspose {
    static constexpr std::string_view name = "GTr";
    static constexpr uint8_t id = 0xE9;
    int8_t semitones;
};

struct VcmdPerVoiceTranspose {
    static constexpr std::string_view name = "PTr";
    static constexpr uint8_t id = 0xEA;
    int8_t semitones;
};

struct VcmdTremoloOn {
    static constexpr std::string_view name = "TOn";
    static constexpr uint8_t id = 0xEB;
    uint8_t delay;
    uint8_t rate;
    uint8_t depth;
};

struct VcmdTremoloOff {
    static constexpr std::string_view name = "TOf";
    static constexpr uint8_t id = 0xEC;
};

struct VcmdVolume {
    static constexpr std::string_view name = "Vol";
    static constexpr uint8_t id = 0xED;
    uint8_t volume;
};

struct VcmdVolumeFade {
    static constexpr std::string_view name = "VFd";
    static constexpr uint8_t id = 0xEE;
    uint8_t time;
    uint8_t target;
};

struct VcmdSubroutineCall {
    static constexpr std::string_view name = "Cal";
    static constexpr uint8_t id = 0xEF;
    int subroutineId;
    uint16_t originalAddr;
    uint8_t count;
};

struct VcmdVibratoFadeIn {
    static constexpr std::string_view name = "Vfi";
    static constexpr uint8_t id = 0xF0;
    uint8_t time;
};

struct VcmdPitchEnvelopeTo {
    static constexpr std::string_view name = "PEt";
    static constexpr uint8_t id = 0xF1;
    uint8_t delay;
    uint8_t length;
    uint8_t semitone;
};

struct VcmdPitchEnvelopeFrom {
    static constexpr std::string_view name = "PEf";
    static constexpr uint8_t id = 0xF2;
    uint8_t delay;
    uint8_t length;
    uint8_t semitone;
};

struct VcmdPitchEnvelopeOff {
    static constexpr std::string_view name = "PEo";
    static constexpr uint8_t id = 0xF3;
};

struct VcmdFineTune {
    static constexpr std::string_view name = "FTn";
    static constexpr uint8_t id = 0xF4;
    int8_t semitones;
};

struct VcmdEchoOn {
    static constexpr std::string_view name = "EOn";
    static constexpr uint8_t id = 0xF5;
    uint8_t channels;
    uint8_t left;
    uint8_t right;
};

struct VcmdEchoOff {
    static constexpr std::string_view name = "EOf";
    static constexpr uint8_t id = 0xF6;
};

struct VcmdEchoParams {
    static constexpr std::string_view name = "EPr";
    static constexpr uint8_t id = 0xF7;
    uint8_t delay;
    uint8_t feedback;
    uint8_t firIndex;
};

struct VcmdEchoVolumeFade {
    static constexpr std::string_view name = "EVF";
    static constexpr uint8_t id = 0xF8;
    uint8_t time;
    uint8_t leftTarget;
    uint8_t rightTarget;
};

struct VcmdPitchSlideToNote {
    static constexpr std::string_view name = "PSt";
    static constexpr uint8_t id = 0xF9;
    uint8_t delay;
    uint8_t length;
    uint8_t note;
};

struct VcmdPercussionBaseInstrument {
    static constexpr std::string_view name = "PIn";
    static constexpr uint8_t id = 0xFA;
    uint8_t index;
};

struct VcmdNOP {
    static constexpr std::string_view name = "NOP";
    static constexpr uint8_t id = 0xFB;
    uint16_t nopBytes;
};

struct VcmdMuteChannel {
    static constexpr std::string_view name = "MCh";
    static constexpr uint8_t id = 0xFC;
};

struct VcmdFastForwardOn {
    static constexpr std::string_view name = "FFo";
    static constexpr uint8_t id = 0xFD;
};

struct VcmdFastForwardOff {
    static constexpr std::string_view name = "FFf";
    static constexpr uint8_t id = 0xFE;
};

struct VcmdUnused {
    static constexpr std::string_view name = "Unu";
    static constexpr uint8_t id = 0xFF;
};

struct VcmdExtension {
    static constexpr std::string_view name = "Ext";
    uint8_t id = 0;
    std::array<uint8_t, 4> params{};
    uint8_t paramCount = 0;
};

struct Vcmd {
    std::variant<std::monostate, VcmdInst, VcmdPanning, VcmdPanFade, VcmdVibratoOn, VcmdVibratoOff, VcmdGlobalVolume,
                 VcmdGlobalVolumeFade, VcmdTempo, VcmdTempoFade, VcmdGlobalTranspose, VcmdPerVoiceTranspose,
                 VcmdTremoloOn, VcmdTremoloOff, VcmdVolume, VcmdVolumeFade, VcmdSubroutineCall, VcmdVibratoFadeIn,
                 VcmdPitchEnvelopeTo, VcmdPitchEnvelopeFrom, VcmdPitchEnvelopeOff, VcmdFineTune, VcmdEchoOn,
                 VcmdEchoOff, VcmdEchoParams, VcmdEchoVolumeFade, VcmdPitchSlideToNote, VcmdPercussionBaseInstrument,
                 VcmdNOP, VcmdMuteChannel, VcmdFastForwardOn, VcmdFastForwardOff, VcmdUnused, VcmdExtension>
        vcmd;
};

/// Returns the number of parameter bytes for a vcmd command (0xE0-0xFF).
uint8_t vcmdParamByteCount(uint8_t cmd);

/// Constructs a Vcmd from a raw command ID and parameter bytes.
/// Returns nullopt for unrecognized or non-constructable IDs (E0, ED, EF, FB).
std::optional<Vcmd> constructVcmd(uint8_t id, const uint8_t* params);
std::optional<Vcmd> constructVcmdForEngine(uint8_t id, const uint8_t* params, const NspcEngineConfig& engine);

/// Returns the 3-char abbreviation for a vcmd ID, or nullptr if invalid.
const char* vcmdNameForId(uint8_t id);

struct Note {
    uint8_t pitch;
};
struct Tie {};
struct Rest {};
struct Percussion {
    uint8_t index;
};
struct Subroutine {
    int id;
    uint16_t originalAddr;
};
struct End {};
using NspcEvent = std::variant<std::monostate, Duration, Vcmd, Note, Tie, Rest, Percussion, Subroutine, End>;

using NspcEventId = uint64_t;

enum class NspcEventOwner : uint8_t {
    Track,
    Subroutine,
};

struct NspcEventRef {
    NspcEventOwner owner = NspcEventOwner::Track;
    int ownerId = -1;
    size_t eventIndex = 0;
    NspcEventId eventId = 0;
};

struct NspcEventEntry {
    NspcEventId id = 0;
    NspcEvent event{};
    std::optional<uint16_t> originalAddr;  // Informational parse-time source address
};

struct NspcSubroutine {
    int id;
    std::vector<NspcEventEntry> events;
    uint16_t originalAddr;
};

struct NspcTrack {
    int id;
    std::vector<NspcEventEntry> events;
    uint16_t originalAddr;
};

struct NspcPattern {
    int id;
    std::optional<std::array<int, 8>> channelTrackIds;
    uint16_t trackTableAddr;
};

struct SequenceTarget {
    std::optional<int> index;
    uint16_t addr;
};

struct PlayPattern {
    int patternId;
    uint16_t trackTableAddr;
};

struct JumpTimes {
    uint8_t count;
    SequenceTarget target;
};

struct AlwaysJump {
    uint8_t opcode;
    SequenceTarget target;
};

struct FastForwardOn {};

struct FastForwardOff {};

struct EndSequence {};

using NspcSequenceOp = std::variant<PlayPattern, JumpTimes, AlwaysJump, FastForwardOn, FastForwardOff, EndSequence>;

struct BrrSample {
    int id;
    std::string name;
    std::vector<uint8_t> data;
    uint16_t originalAddr;
    uint16_t originalLoopAddr;
    NspcContentOrigin contentOrigin = NspcContentOrigin::EngineProvided;
};

struct NspcInstrument {
    int id;
    uint8_t sampleIndex;
    uint8_t adsr1;
    uint8_t adsr2;
    uint8_t gain;
    uint8_t basePitchMult;
    uint8_t fracPitchMult;
    uint8_t percussionNote = 0;
    std::string name;
    uint16_t originalAddr;
    NspcContentOrigin contentOrigin = NspcContentOrigin::EngineProvided;
};

class NspcSong {
public:
    /// Default constructor for testing purposes
    NspcSong() = default;

    NspcSong(emulation::AramView aram, const NspcEngineConfig& config, int songIndex);
    static NspcSong createEmpty(int songId);

    const std::vector<NspcSequenceOp>& sequence() const { return sequence_; }

    std::vector<NspcSequenceOp>& sequence() { return sequence_; }

    const std::vector<NspcPattern>& patterns() const { return patterns_; }

    std::vector<NspcPattern>& patterns() { return patterns_; }

    const std::vector<NspcTrack>& tracks() const { return tracks_; }

    std::vector<NspcTrack>& tracks() { return tracks_; }

    const std::vector<NspcSubroutine>& subroutines() const { return subroutines_; }

    std::vector<NspcSubroutine>& subroutines() { return subroutines_; }

    std::optional<int> loopPatternIndex() const { return loopPatternIndex_; }

    int songId() const { return songId_; }
    void setSongId(int songId) { songId_ = songId; }
    const std::string& songName() const { return songName_; }
    void setSongName(std::string songName) { songName_ = std::move(songName); }
    const std::string& author() const { return author_; }
    void setAuthor(std::string author) { author_ = std::move(author); }
    NspcContentOrigin contentOrigin() const { return contentOrigin_; }
    void setContentOrigin(NspcContentOrigin contentOrigin) { contentOrigin_ = contentOrigin; }
    bool isUserProvided() const { return contentOrigin_ == NspcContentOrigin::UserProvided; }
    bool isEngineProvided() const { return contentOrigin_ == NspcContentOrigin::EngineProvided; }

    NspcEventId peekNextEventId() const { return nextEventId_; }
    void setNextEventId(NspcEventId id) { nextEventId_ = id; }

    const NspcEvent* resolveEvent(const NspcEventRef& ref) const;
    NspcEvent* resolveEvent(const NspcEventRef& ref);
    bool replaceEvent(const NspcEventRef& ref, const NspcEvent& replacement);
    /// Inline subroutine call sites into tracks where possible and drop subroutine data
    /// when all calls have been resolved.
    void flattenSubroutines();

private:
    void parsePattern(emulation::AramView aram, uint16_t patternAddr, int patternIndex);
    void parseTrack(emulation::AramView aram, uint16_t trackAddr, int trackIndex,
                    std::optional<uint16_t> hardStopExclusive = std::nullopt);
    std::vector<NspcEventEntry> parseEvents(emulation::AramView aram, uint16_t startAddr, uint16_t& endAddr,
                                            std::optional<uint16_t> hardStopExclusive = std::nullopt);
    Vcmd parseVcmd(emulation::AramView aram, uint16_t& addr);

    int songId_ = 0;
    std::string songName_;
    std::string author_;
    NspcCommandMap commandMap_{};
    std::unordered_map<uint8_t, uint8_t> extensionParamCountById_;

    std::vector<NspcTrack> tracks_;
    int nextTrackId_ = 0;

    std::vector<NspcSubroutine> subroutines_;
    int nextSubroutineId_ = 0;

    std::vector<NspcPattern> patterns_;
    int nextPatternId_ = 0;

    std::optional<int> loopPatternIndex_;

    std::vector<NspcSequenceOp> sequence_;

    std::unordered_map<uint16_t, int> trackAddrToIndex_;
    std::unordered_map<uint16_t, int> subroutineAddrToIndex_;
    NspcEventId nextEventId_ = 1;
    NspcContentOrigin contentOrigin_ = NspcContentOrigin::EngineProvided;
};

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

}  // namespace ntrak::nspc
