#include "ntrak/nspc/MidiImport.hpp"

#include "ntrak/nspc/BrrCodec.hpp"
#include "ntrak/nspc/NspcAssetFile.hpp"
#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcConverter.hpp"
#include "ntrak/nspc/NspcEngine.hpp"

#include <MidiFile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ntrak::nspc {
namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr int kMaxNspcChannels = 8;
constexpr int kNspcTicksPerQuarter = 48;
constexpr int kPatternBars = 4;
constexpr int kBeatsPerBar = 4;
constexpr int kPatternLengthNspcTicks = kPatternBars * kBeatsPerBar * kNspcTicksPerQuarter;  // 768
constexpr double kTempoScaleDivisor = 4.8;
constexpr uint8_t kDefaultQvByte = 0x7F;
constexpr double kDefaultBpm = 120.0;

// GM program names (General MIDI Level 1)
constexpr const char* kGmProgramNames[] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
    "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
    "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
    "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
    "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar harmonics",
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass",
    "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass",
    "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "SynthStrings 1", "SynthStrings 2",
    "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
    "French Horn", "Brass Section", "SynthBrass 1", "SynthBrass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
    "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute",
    "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)",
    "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
    "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
    "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    "Sitar", "Banjo", "Shamisen", "Koto",
    "Kalimba", "Bag pipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
    "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
    "Telephone Ring", "Helicopter", "Applause", "Gunshot",
};

// NSPC note names for display
constexpr const char* kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// ---------------------------------------------------------------------------
// Warning collector (same pattern as ItImport.cpp)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Internal MIDI representation
// ---------------------------------------------------------------------------

struct MidiNoteEvent {
    int tickAbsolute = 0;
    int midiChannel = 0;
    int noteNumber = 0;
    int velocity = 0;
    int durationTicks = 0;  // from linked note-off
};

struct MidiTempoEvent {
    int tickAbsolute = 0;
    double bpm = kDefaultBpm;
};

struct MidiCcEvent {
    int tickAbsolute = 0;
    int midiChannel = 0;
    int ccNumber = 0;
    int value = 0;
};

struct MidiProgramEvent {
    int tickAbsolute = 0;
    int midiChannel = 0;
    int program = 0;
};

struct ParsedMidiFile {
    std::string fileName;
    int format = 0;
    int ppq = 480;
    int trackCount = 0;
    int totalLengthTicks = 0;

    std::vector<MidiNoteEvent> notes;
    std::vector<MidiTempoEvent> tempoMap;
    std::vector<MidiCcEvent> ccEvents;
    std::vector<MidiProgramEvent> programChanges;
};

// ---------------------------------------------------------------------------
// MIDI parsing using midifile library
// ---------------------------------------------------------------------------

std::expected<ParsedMidiFile, std::string> loadMidiFile(const std::filesystem::path& path) {
    smf::MidiFile midifile;
    if (!midifile.read(path.string())) {
        return std::unexpected(std::format("Failed to read MIDI file: {}", path.filename().string()));
    }

    midifile.doTimeAnalysis();
    midifile.linkNotePairs();

    // Sort all tracks into absolute tick order (joinTracks merges into track 0)
    midifile.joinTracks();

    ParsedMidiFile parsed;
    parsed.fileName = path.filename().string();
    parsed.ppq = midifile.getTicksPerQuarterNote();
    parsed.trackCount = midifile.getTrackCount();

    if (parsed.ppq <= 0) {
        return std::unexpected("MIDI file has invalid ticks-per-quarter-note value");
    }

    // After joinTracks, everything is in track 0
    const auto& track = midifile[0];
    const int eventCount = track.getEventCount();

    for (int i = 0; i < eventCount; ++i) {
        const auto& event = track[i];

        if (event.isNoteOn() && event.getVelocity() > 0) {
            MidiNoteEvent note;
            note.tickAbsolute = event.tick;
            note.midiChannel = event.getChannel();
            note.noteNumber = event.getKeyNumber();
            note.velocity = event.getVelocity();
            note.durationTicks = event.isLinked() ? event.getTickDuration() : parsed.ppq;  // default quarter note
            parsed.notes.push_back(note);

            if (event.tick + note.durationTicks > parsed.totalLengthTicks) {
                parsed.totalLengthTicks = event.tick + note.durationTicks;
            }
        } else if (event.isTempo()) {
            MidiTempoEvent tempo;
            tempo.tickAbsolute = event.tick;
            tempo.bpm = event.getTempoBPM();
            parsed.tempoMap.push_back(tempo);
        } else if (event.isController()) {
            MidiCcEvent cc;
            cc.tickAbsolute = event.tick;
            cc.midiChannel = event.getChannel();
            cc.ccNumber = event.getControllerNumber();
            cc.value = event.getControllerValue();
            parsed.ccEvents.push_back(cc);
        } else if (event.isPatchChange()) {
            MidiProgramEvent pc;
            pc.tickAbsolute = event.tick;
            pc.midiChannel = event.getChannel();
            pc.program = event.getP1();
            parsed.programChanges.push_back(pc);
        }
    }

    // Sort notes by tick then channel
    std::sort(parsed.notes.begin(), parsed.notes.end(), [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
        if (a.tickAbsolute != b.tickAbsolute) return a.tickAbsolute < b.tickAbsolute;
        if (a.midiChannel != b.midiChannel) return a.midiChannel < b.midiChannel;
        return a.noteNumber < b.noteNumber;
    });

    // Detect format (joinTracks resets track count to 1, but we saved original)
    parsed.format = (parsed.trackCount > 1) ? 1 : 0;

    return parsed;
}

// ---------------------------------------------------------------------------
// Channel detection and labeling
// ---------------------------------------------------------------------------

std::string formatChannelLabel(int midiChannel, int program) {
    if (midiChannel == 9) {
        return "Ch 10 (Drums)";
    }
    std::string label = std::format("Ch {:d}", midiChannel + 1);
    if (program >= 0 && program < 128) {
        label += std::format(" ({})", kGmProgramNames[program]);
    }
    return label;
}

std::string formatMidiNoteRange(int minNote, int maxNote) {
    if (minNote > maxNote) return "---";
    auto noteName = [](int note) -> std::string {
        int octave = (note / 12) - 1;
        return std::format("{}{}", kNoteNames[note % 12], octave);
    };
    return std::format("{}-{}", noteName(minNote), noteName(maxNote));
}

// ---------------------------------------------------------------------------
// Timing conversion
// ---------------------------------------------------------------------------

int midiTickToNspcTick(int midiTick, int ppq) {
    return static_cast<int>(std::lround(static_cast<double>(midiTick) * kNspcTicksPerQuarter / ppq));
}

uint8_t bpmToNspcTempo(double bpm) {
    const int scaled = static_cast<int>(std::lround(bpm / kTempoScaleDivisor));
    return static_cast<uint8_t>(std::clamp(scaled, 1, 0xFF));
}

int mapMidiNoteToNspcPitch(int midiNote, int8_t transposeOctaves) {
    const int shifted = midiNote - 24 + (transposeOctaves * 12);
    return std::clamp(shifted, 0, 0x47);
}

uint8_t mapMidiVelocityToVolume(int velocity) {
    return static_cast<uint8_t>(std::clamp(velocity * 2, 0, 0xFF));
}

uint8_t mapMidiPanToNspc(int midiPan) {
    // MIDI pan: 0=left, 64=center, 127=right
    // NSPC pan: 0x00=left, 0x0A=center, 0x14=right
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(midiPan * 20.0 / 127.0)), 0, 0x14));
}

// ---------------------------------------------------------------------------
// ARAM estimation helper
// ---------------------------------------------------------------------------

uint32_t estimateFreeAfterDeletion(const NspcProject& baseProject, const MidiImportOptions& options) {
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

// ---------------------------------------------------------------------------
// Instrument and sample creation from mapping options
// ---------------------------------------------------------------------------

struct CreatedAsset {
    NspcInstrument instrument;
    BrrSample sample;
    bool hasSample = false;
};

std::expected<std::vector<CreatedAsset>, std::string>
buildAssetsFromMappings(const MidiImportOptions& options, WarningCollector& warnings) {
    std::vector<CreatedAsset> assets;
    int nextId = 0;

    for (const auto& mapping : options.channelMappings) {
        if (!mapping.enabled) continue;
        if (mapping.instrumentSource.kind == MidiInstrumentSource::Kind::MapToExisting) continue;

        CreatedAsset asset;
        asset.instrument.id = nextId;
        asset.instrument.name = mapping.channelLabel;
        asset.instrument.originalAddr = 0;
        asset.instrument.contentOrigin = NspcContentOrigin::UserProvided;

        switch (mapping.instrumentSource.kind) {
            case MidiInstrumentSource::Kind::CreateBlank: {
                // Default ADSR: moderate attack, full sustain
                asset.instrument.adsr1 = 0x8F;
                asset.instrument.adsr2 = 0xE0;
                asset.instrument.gain = 0;
                asset.instrument.sampleIndex = static_cast<uint8_t>(nextId);
                asset.instrument.basePitchMult = 1;
                asset.instrument.fracPitchMult = 0;

                // Create a silent BRR sample (single block, loop disabled)
                BrrSample silentSample;
                silentSample.id = nextId;
                silentSample.name = std::format("MIDI Ch{} (blank)", mapping.midiChannel + 1);
                silentSample.data = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // end block
                silentSample.originalAddr = 0;
                silentSample.originalLoopAddr = 0;
                silentSample.contentOrigin = NspcContentOrigin::UserProvided;
                asset.sample = std::move(silentSample);
                asset.hasSample = true;
                break;
            }

            case MidiInstrumentSource::Kind::FromBrrFile: {
                if (!mapping.instrumentSource.assetPath.has_value()) {
                    warnings.add(std::format("MIDI channel {} has no BRR file path set; creating blank instrument",
                                             mapping.midiChannel + 1));
                    asset.instrument.adsr1 = 0x8F;
                    asset.instrument.adsr2 = 0xE0;
                    asset.instrument.gain = 0;
                    asset.instrument.sampleIndex = static_cast<uint8_t>(nextId);
                    asset.instrument.basePitchMult = 1;
                    asset.instrument.fracPitchMult = 0;

                    BrrSample silentSample;
                    silentSample.id = nextId;
                    silentSample.name = std::format("MIDI Ch{} (blank)", mapping.midiChannel + 1);
                    silentSample.data = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                    silentSample.originalAddr = 0;
                    silentSample.originalLoopAddr = 0;
                    silentSample.contentOrigin = NspcContentOrigin::UserProvided;
                    asset.sample = std::move(silentSample);
                    asset.hasSample = true;
                    break;
                }

                auto brrData = loadBrrFile(*mapping.instrumentSource.assetPath);
                if (!brrData.has_value()) {
                    return std::unexpected(
                        std::format("Failed to load BRR file for channel {}: {}", mapping.midiChannel + 1, brrData.error()));
                }

                asset.instrument.adsr1 = 0x8F;
                asset.instrument.adsr2 = 0xE0;
                asset.instrument.gain = 0;
                asset.instrument.sampleIndex = static_cast<uint8_t>(nextId);
                asset.instrument.basePitchMult = 1;
                asset.instrument.fracPitchMult = 0;

                BrrSample brrSample;
                brrSample.id = nextId;
                brrSample.name = mapping.instrumentSource.assetPath->stem().string();
                brrSample.data = std::move(*brrData);
                brrSample.originalAddr = 0;
                brrSample.originalLoopAddr = 0;
                brrSample.contentOrigin = NspcContentOrigin::UserProvided;
                asset.sample = std::move(brrSample);
                asset.hasSample = true;
                break;
            }

            case MidiInstrumentSource::Kind::FromNtiFile: {
                if (!mapping.instrumentSource.assetPath.has_value()) {
                    warnings.add(std::format("MIDI channel {} has no NTI file path set; creating blank instrument",
                                             mapping.midiChannel + 1));
                    asset.instrument.adsr1 = 0x8F;
                    asset.instrument.adsr2 = 0xE0;
                    asset.instrument.gain = 0;
                    asset.instrument.sampleIndex = static_cast<uint8_t>(nextId);
                    asset.instrument.basePitchMult = 1;
                    asset.instrument.fracPitchMult = 0;

                    BrrSample silentSample;
                    silentSample.id = nextId;
                    silentSample.name = std::format("MIDI Ch{} (blank)", mapping.midiChannel + 1);
                    silentSample.data = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                    silentSample.originalAddr = 0;
                    silentSample.originalLoopAddr = 0;
                    silentSample.contentOrigin = NspcContentOrigin::UserProvided;
                    asset.sample = std::move(silentSample);
                    asset.hasSample = true;
                    break;
                }

                auto ntiData = loadNtiFile(*mapping.instrumentSource.assetPath);
                if (!ntiData.has_value()) {
                    return std::unexpected(
                        std::format("Failed to load NTI file for channel {}: {}", mapping.midiChannel + 1, ntiData.error()));
                }

                asset.instrument = ntiData->instrument;
                asset.instrument.id = nextId;
                asset.instrument.sampleIndex = static_cast<uint8_t>(nextId);
                asset.instrument.contentOrigin = NspcContentOrigin::UserProvided;

                asset.sample = std::move(ntiData->sample);
                asset.sample.id = nextId;
                asset.sample.contentOrigin = NspcContentOrigin::UserProvided;
                asset.hasSample = true;
                break;
            }

            case MidiInstrumentSource::Kind::MapToExisting:
                break;  // handled above
        }

        assets.push_back(std::move(asset));
        ++nextId;
    }

    return assets;
}

// ---------------------------------------------------------------------------
// Event emission helpers
// ---------------------------------------------------------------------------

NspcEventEntry makeEvent(NspcEventId& nextEventId, NspcEvent event) {
    NspcEventEntry entry;
    entry.id = nextEventId++;
    entry.event = std::move(event);
    return entry;
}

void emitDuration(std::vector<NspcEventEntry>& events, NspcEventId& nextEventId, int ticks) {
    while (ticks > 0) {
        const int chunk = std::min(ticks, 0x7F);
        events.push_back(makeEvent(nextEventId, Duration{
            .ticks = static_cast<uint8_t>(chunk),
            .quantization = std::nullopt,
            .velocity = std::nullopt,
        }));
        ticks -= chunk;
    }
}

void emitNote(std::vector<NspcEventEntry>& events, NspcEventId& nextEventId, int nspcPitch, int durationTicks) {
    if (durationTicks <= 0) return;

    // First chunk: Duration + Note
    const int firstChunk = std::min(durationTicks, 0x7F);
    events.push_back(makeEvent(nextEventId, Duration{
        .ticks = static_cast<uint8_t>(firstChunk),
        .quantization = std::nullopt,
        .velocity = std::nullopt,
    }));
    events.push_back(makeEvent(nextEventId, Note{.pitch = static_cast<uint8_t>(nspcPitch)}));

    // Remaining chunks: Duration + Tie
    int remaining = durationTicks - firstChunk;
    while (remaining > 0) {
        const int chunk = std::min(remaining, 0x7F);
        events.push_back(makeEvent(nextEventId, Duration{
            .ticks = static_cast<uint8_t>(chunk),
            .quantization = std::nullopt,
            .velocity = std::nullopt,
        }));
        events.push_back(makeEvent(nextEventId, Tie{}));
        remaining -= chunk;
    }
}

void emitRest(std::vector<NspcEventEntry>& events, NspcEventId& nextEventId, int durationTicks) {
    if (durationTicks <= 0) return;

    while (durationTicks > 0) {
        const int chunk = std::min(durationTicks, 0x7F);
        events.push_back(makeEvent(nextEventId, Duration{
            .ticks = static_cast<uint8_t>(chunk),
            .quantization = std::nullopt,
            .velocity = std::nullopt,
        }));
        events.push_back(makeEvent(nextEventId, Rest{}));
        durationTicks -= chunk;
    }
}

void emitVcmd(std::vector<NspcEventEntry>& events, NspcEventId& nextEventId, auto vcmd) {
    events.push_back(makeEvent(nextEventId, Vcmd{.vcmd = std::move(vcmd)}));
}

// ---------------------------------------------------------------------------
// Channel note conversion (for one channel within one pattern window)
// ---------------------------------------------------------------------------

struct ChannelState {
    int currentVolume = -1;
    int currentPanning = -1;
    int currentInstrument = -1;
};

NspcTrack convertChannelPatternToTrack(
    const ParsedMidiFile& midi,
    const std::vector<MidiNoteEvent>& channelNotes,
    const std::vector<MidiCcEvent>& channelCcs,
    int nspcInstrumentId,
    const MidiChannelMapping& mapping,
    const MidiImportOptions& options,
    int patternStartMidiTick,
    int patternEndMidiTick,
    int trackId,
    NspcEventId& nextEventId,
    ChannelState& state,
    WarningCollector& warnings) {

    NspcTrack track;
    track.id = trackId;
    track.originalAddr = 0;

    const int patternStartNspc = midiTickToNspcTick(patternStartMidiTick, midi.ppq);
    const int patternEndNspc = midiTickToNspcTick(patternEndMidiTick, midi.ppq);
    const int patternLength = patternEndNspc - patternStartNspc;

    if (patternLength <= 0) return track;

    // Emit instrument select if needed
    if (state.currentInstrument != nspcInstrumentId) {
        emitVcmd(track.events, nextEventId, VcmdInst{.instrumentIndex = static_cast<uint8_t>(nspcInstrumentId)});
        state.currentInstrument = nspcInstrumentId;
    }

    // Collect CC#10 (pan) events in this time range
    if (options.convertPanCc) {
        for (const auto& cc : channelCcs) {
            if (cc.ccNumber != 10) continue;
            if (cc.tickAbsolute < patternStartMidiTick || cc.tickAbsolute >= patternEndMidiTick) continue;
            const int nspcPan = mapMidiPanToNspc(cc.value);
            if (nspcPan != state.currentPanning) {
                // We'll handle pan inline with notes below for proper timing
                // For simplicity, set it at the start of the pattern
                emitVcmd(track.events, nextEventId, VcmdPanning{.panning = static_cast<uint8_t>(nspcPan)});
                state.currentPanning = nspcPan;
                break;  // Only emit first pan change per pattern for simplicity
            }
        }
    }

    // Filter notes for this pattern window
    struct NspcNoteEvent {
        int startNspc = 0;  // relative to pattern start
        int durationNspc = 0;
        int nspcPitch = 0;
        int velocity = 0;
    };

    std::vector<NspcNoteEvent> patternNotes;
    for (const auto& note : channelNotes) {
        const int noteEndMidi = note.tickAbsolute + note.durationTicks;

        // Skip if note doesn't overlap this pattern
        if (note.tickAbsolute >= patternEndMidiTick || noteEndMidi <= patternStartMidiTick) continue;

        // Clamp to pattern boundaries
        const int clampedStartMidi = std::max(note.tickAbsolute, patternStartMidiTick);
        const int clampedEndMidi = std::min(noteEndMidi, patternEndMidiTick);

        NspcNoteEvent nspcNote;
        nspcNote.startNspc = midiTickToNspcTick(clampedStartMidi, midi.ppq) - patternStartNspc;
        const int endNspc = midiTickToNspcTick(clampedEndMidi, midi.ppq) - patternStartNspc;
        nspcNote.durationNspc = std::max(endNspc - nspcNote.startNspc, 1);
        nspcNote.nspcPitch = mapMidiNoteToNspcPitch(note.noteNumber, mapping.transposeOctaves);
        nspcNote.velocity = note.velocity;
        patternNotes.push_back(nspcNote);
    }

    // Sort by start time; for polyphony (same tick), keep latest note (highest index = most recent)
    std::sort(patternNotes.begin(), patternNotes.end(),
              [](const NspcNoteEvent& a, const NspcNoteEvent& b) { return a.startNspc < b.startNspc; });

    // Resolve polyphony: keep latest note at each tick position (mono-synth behavior)
    std::vector<NspcNoteEvent> monoNotes;
    for (size_t i = 0; i < patternNotes.size(); ++i) {
        // If next note starts at the same tick, skip this one (keep the last one)
        if (i + 1 < patternNotes.size() && patternNotes[i + 1].startNspc == patternNotes[i].startNspc) {
            continue;
        }
        monoNotes.push_back(patternNotes[i]);
    }

    // Also truncate overlapping notes (new note cuts previous)
    for (size_t i = 0; i + 1 < monoNotes.size(); ++i) {
        const int maxDuration = monoNotes[i + 1].startNspc - monoNotes[i].startNspc;
        if (monoNotes[i].durationNspc > maxDuration) {
            monoNotes[i].durationNspc = maxDuration;
        }
    }

    // Clamp last note to pattern boundary
    if (!monoNotes.empty()) {
        auto& last = monoNotes.back();
        if (last.startNspc + last.durationNspc > patternLength) {
            last.durationNspc = patternLength - last.startNspc;
        }
    }

    // Emit notes with proper gaps
    int cursor = 0;  // current position in NSPC ticks relative to pattern start
    for (const auto& note : monoNotes) {
        // Emit rest for gap before this note
        if (note.startNspc > cursor) {
            emitRest(track.events, nextEventId, note.startNspc - cursor);
        }

        // Emit velocity/volume change if needed
        if (options.convertVelocityToVolume) {
            const int vol = mapMidiVelocityToVolume(note.velocity);
            if (vol != state.currentVolume) {
                emitVcmd(track.events, nextEventId, VcmdVolume{.volume = static_cast<uint8_t>(vol)});
                state.currentVolume = vol;
            }
        }

        // Emit the note
        if (note.durationNspc > 0) {
            emitNote(track.events, nextEventId, note.nspcPitch, note.durationNspc);
        }

        cursor = note.startNspc + note.durationNspc;
    }

    // Fill remaining pattern time with rest
    if (cursor < patternLength) {
        emitRest(track.events, nextEventId, patternLength - cursor);
    }

    // End marker so the engine knows the track is finished
    track.events.push_back(makeEvent(nextEventId, End{}));

    return track;
}

// ---------------------------------------------------------------------------
// Check if a track has any meaningful data (not just rests)
// ---------------------------------------------------------------------------

bool trackHasNonRestData(const NspcTrack& track) {
    for (const auto& entry : track.events) {
        if (std::holds_alternative<Note>(entry.event) || std::holds_alternative<Vcmd>(entry.event)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Core conversion: parsed MIDI → NspcSong + instruments + samples
// ---------------------------------------------------------------------------

struct ConversionResult {
    NspcSong song;
    std::vector<NspcInstrument> instruments;
    std::vector<BrrSample> samples;
    std::vector<std::string> warnings;
};

std::expected<ConversionResult, std::string>
convertMidiToNspc(const ParsedMidiFile& midi, const MidiImportOptions& options, const NspcProject& baseProject,
                  WarningCollector& warnings) {
    // Build instrument/sample assets from mappings
    auto assetsResult = buildAssetsFromMappings(options, warnings);
    if (!assetsResult.has_value()) {
        return std::unexpected(assetsResult.error());
    }
    auto& assets = *assetsResult;

    // Build mapping: MIDI channel → NSPC instrument index
    // For MapToExisting channels, the instrument ID is the existing one
    // For created instruments, we assign sequential IDs
    struct ChannelInstrumentInfo {
        int nspcInstrumentId = -1;
        bool isExisting = false;
    };

    std::array<ChannelInstrumentInfo, 16> channelInstrument{};
    int createdIndex = 0;
    for (const auto& mapping : options.channelMappings) {
        if (!mapping.enabled) continue;
        if (mapping.midiChannel < 0 || mapping.midiChannel >= 16) continue;

        if (mapping.instrumentSource.kind == MidiInstrumentSource::Kind::MapToExisting) {
            channelInstrument[static_cast<size_t>(mapping.midiChannel)] = {
                .nspcInstrumentId = mapping.instrumentSource.existingInstrumentId,
                .isExisting = true,
            };
        } else {
            if (createdIndex < static_cast<int>(assets.size())) {
                channelInstrument[static_cast<size_t>(mapping.midiChannel)] = {
                    .nspcInstrumentId = assets[static_cast<size_t>(createdIndex)].instrument.id,
                    .isExisting = false,
                };
                ++createdIndex;
            }
        }
    }

    // Build SNES channel assignment: MIDI channel → NSPC channel (0-7)
    std::array<int, 16> midiToNspcChannel{};
    midiToNspcChannel.fill(-1);

    // First pass: assign explicit assignments
    std::array<bool, kMaxNspcChannels> nspcChannelUsed{};
    nspcChannelUsed.fill(false);

    for (const auto& mapping : options.channelMappings) {
        if (!mapping.enabled) continue;
        if (mapping.targetNspcChannel >= 0 && mapping.targetNspcChannel < kMaxNspcChannels) {
            if (nspcChannelUsed[static_cast<size_t>(mapping.targetNspcChannel)]) {
                warnings.add(std::format("SNES channel {} is assigned to multiple MIDI channels", mapping.targetNspcChannel));
            }
            midiToNspcChannel[static_cast<size_t>(mapping.midiChannel)] = mapping.targetNspcChannel;
            nspcChannelUsed[static_cast<size_t>(mapping.targetNspcChannel)] = true;
        }
    }

    // Second pass: auto-assign remaining
    for (const auto& mapping : options.channelMappings) {
        if (!mapping.enabled) continue;
        if (midiToNspcChannel[static_cast<size_t>(mapping.midiChannel)] >= 0) continue;

        int freeChannel = -1;
        for (int ch = 0; ch < kMaxNspcChannels; ++ch) {
            if (!nspcChannelUsed[static_cast<size_t>(ch)]) {
                freeChannel = ch;
                break;
            }
        }
        if (freeChannel < 0) {
            warnings.add(std::format("No free SNES channel for MIDI channel {}; skipping", mapping.midiChannel + 1));
            continue;
        }
        midiToNspcChannel[static_cast<size_t>(mapping.midiChannel)] = freeChannel;
        nspcChannelUsed[static_cast<size_t>(freeChannel)] = true;
    }

    // Pre-filter notes and CCs by channel for efficient access
    std::array<std::vector<MidiNoteEvent>, 16> notesByChannel;
    std::array<std::vector<MidiCcEvent>, 16> ccsByChannel;

    for (const auto& note : midi.notes) {
        if (note.midiChannel >= 0 && note.midiChannel < 16) {
            notesByChannel[static_cast<size_t>(note.midiChannel)].push_back(note);
        }
    }
    for (const auto& cc : midi.ccEvents) {
        if (cc.midiChannel >= 0 && cc.midiChannel < 16) {
            ccsByChannel[static_cast<size_t>(cc.midiChannel)].push_back(cc);
        }
    }

    // Determine pattern boundaries
    const int totalMidiTicks = midi.totalLengthTicks;
    const int patternLengthMidiTicks = kPatternLengthNspcTicks * midi.ppq / kNspcTicksPerQuarter;
    const int patternCount =
        std::max(1, static_cast<int>(std::ceil(static_cast<double>(totalMidiTicks) / patternLengthMidiTicks)));

    // Build tempo events in NSPC tick space
    std::vector<std::pair<int, uint8_t>> nspcTempoEvents;  // (nspcTick, tempoValue)
    for (const auto& tempo : midi.tempoMap) {
        nspcTempoEvents.emplace_back(midiTickToNspcTick(tempo.tickAbsolute, midi.ppq), bpmToNspcTempo(tempo.bpm));
    }
    if (nspcTempoEvents.empty()) {
        nspcTempoEvents.emplace_back(0, bpmToNspcTempo(kDefaultBpm));
    }

    // Convert patterns and tracks
    NspcSong song{};
    song.setSongId(0);
    song.setSongName(midi.fileName);
    song.setContentOrigin(NspcContentOrigin::UserProvided);

    int nextPatternId = 0;
    int nextTrackId = 0;
    NspcEventId nextEventId = 1;

    std::array<ChannelState, 16> channelStates{};
    std::vector<int> orderedPatternIds;
    orderedPatternIds.reserve(static_cast<size_t>(patternCount));

    for (int patIdx = 0; patIdx < patternCount; ++patIdx) {
        const int patStartMidi = patIdx * patternLengthMidiTicks;
        const int patEndMidi = (patIdx + 1) * patternLengthMidiTicks;

        std::array<int, kMaxNspcChannels> trackIds{};
        trackIds.fill(-1);

        // Emit tempo events on SNES channel 0 for this pattern
        // (We inject tempo VCMDs into the first active channel's track)
        std::vector<std::pair<int, uint8_t>> patternTempoEvents;
        const int patStartNspc = patIdx * kPatternLengthNspcTicks;
        const int patEndNspc = (patIdx + 1) * kPatternLengthNspcTicks;
        for (const auto& [tick, tempo] : nspcTempoEvents) {
            if (tick >= patStartNspc && tick < patEndNspc) {
                patternTempoEvents.emplace_back(tick - patStartNspc, tempo);
            }
        }

        for (const auto& mapping : options.channelMappings) {
            if (!mapping.enabled) continue;
            const int midiCh = mapping.midiChannel;
            if (midiCh < 0 || midiCh >= 16) continue;
            const int nspcCh = midiToNspcChannel[static_cast<size_t>(midiCh)];
            if (nspcCh < 0) continue;

            const auto& chNotes = notesByChannel[static_cast<size_t>(midiCh)];
            const auto& chCcs = ccsByChannel[static_cast<size_t>(midiCh)];
            const int instId = channelInstrument[static_cast<size_t>(midiCh)].nspcInstrumentId;

            if (instId < 0) continue;

            // Check if this channel has any notes in this pattern window
            bool hasNotesInPattern = false;
            for (const auto& note : chNotes) {
                const int noteEnd = note.tickAbsolute + note.durationTicks;
                if (note.tickAbsolute < patEndMidi && noteEnd > patStartMidi) {
                    hasNotesInPattern = true;
                    break;
                }
            }

            if (!hasNotesInPattern && patIdx > 0) continue;  // Skip empty patterns (except first for setup)

            NspcTrack track = convertChannelPatternToTrack(
                midi, chNotes, chCcs, instId, mapping, options, patStartMidi, patEndMidi, nextTrackId, nextEventId,
                channelStates[static_cast<size_t>(midiCh)], warnings);

            // Inject tempo events into the first channel's track
            if (nspcCh == 0 || trackIds[0] == -1) {
                // Insert tempo VCMDs at the beginning of the track for this pattern
                for (const auto& [relTick, tempoVal] : patternTempoEvents) {
                    if (relTick == 0) {
                        // Prepend tempo at track start
                        auto tempoEvent = makeEvent(nextEventId, Vcmd{.vcmd = VcmdTempo{.tempo = tempoVal}});
                        track.events.insert(track.events.begin(), std::move(tempoEvent));
                    }
                    // Mid-pattern tempo changes are complex; for v1 we only handle tempo at pattern boundaries
                }
            }

            if (!trackHasNonRestData(track) && patIdx > 0) continue;

            track.id = nextTrackId;
            trackIds[static_cast<size_t>(nspcCh)] = nextTrackId;
            song.tracks().push_back(std::move(track));
            ++nextTrackId;
        }

        const int songPatternId = nextPatternId++;
        orderedPatternIds.push_back(songPatternId);
        song.patterns().push_back(NspcPattern{
            .id = songPatternId,
            .channelTrackIds = trackIds,
            .trackTableAddr = 0,
        });
    }

    // Build sequence
    for (const int patId : orderedPatternIds) {
        song.sequence().push_back(PlayPattern{
            .patternId = patId,
            .trackTableAddr = 0,
        });
    }
    song.sequence().push_back(EndSequence{});

    // Collect instruments and samples
    std::vector<NspcInstrument> instruments;
    std::vector<BrrSample> samples;
    for (auto& asset : assets) {
        instruments.push_back(std::move(asset.instrument));
        if (asset.hasSample) {
            samples.push_back(std::move(asset.sample));
        }
    }

    ConversionResult result;
    result.song = std::move(song);
    result.instruments = std::move(instruments);
    result.samples = std::move(samples);
    return result;
}

// ---------------------------------------------------------------------------
// Channel analysis (for preview and default mappings)
// ---------------------------------------------------------------------------

struct ChannelAnalysis {
    bool hasNotes = false;
    int noteCount = 0;
    int minNote = 127;
    int maxNote = 0;
    int lastProgram = -1;
    bool hasVelocityChanges = false;
    bool hasPanChanges = false;
    int firstVelocity = -1;
    int firstPan = -1;
};

std::array<ChannelAnalysis, 16> analyzeChannels(const ParsedMidiFile& midi) {
    std::array<ChannelAnalysis, 16> analysis{};

    for (const auto& note : midi.notes) {
        if (note.midiChannel < 0 || note.midiChannel >= 16) continue;
        auto& ch = analysis[static_cast<size_t>(note.midiChannel)];
        ch.hasNotes = true;
        ch.noteCount++;
        ch.minNote = std::min(ch.minNote, note.noteNumber);
        ch.maxNote = std::max(ch.maxNote, note.noteNumber);

        if (ch.firstVelocity < 0) {
            ch.firstVelocity = note.velocity;
        } else if (note.velocity != ch.firstVelocity) {
            ch.hasVelocityChanges = true;
        }
    }

    for (const auto& pc : midi.programChanges) {
        if (pc.midiChannel >= 0 && pc.midiChannel < 16) {
            analysis[static_cast<size_t>(pc.midiChannel)].lastProgram = pc.program;
        }
    }

    for (const auto& cc : midi.ccEvents) {
        if (cc.midiChannel < 0 || cc.midiChannel >= 16) continue;
        if (cc.ccNumber == 10) {
            auto& ch = analysis[static_cast<size_t>(cc.midiChannel)];
            if (ch.firstPan < 0) {
                ch.firstPan = cc.value;
            } else if (cc.value != ch.firstPan) {
                ch.hasPanChanges = true;
            }
        }
    }

    return analysis;
}

}  // namespace

// ===========================================================================
// Public API implementation
// ===========================================================================

std::expected<std::vector<MidiChannelMapping>, std::string>
buildDefaultMidiChannelMappings(const std::filesystem::path& midiPath) {
    auto parsed = loadMidiFile(midiPath);
    if (!parsed.has_value()) {
        return std::unexpected(parsed.error());
    }

    const auto analysis = analyzeChannels(*parsed);

    std::vector<MidiChannelMapping> mappings;
    int nspcChannel = 0;

    for (int ch = 0; ch < 16; ++ch) {
        if (!analysis[static_cast<size_t>(ch)].hasNotes) continue;

        MidiChannelMapping m;
        m.midiChannel = ch;
        m.midiProgram = analysis[static_cast<size_t>(ch)].lastProgram;
        m.channelLabel = formatChannelLabel(ch, m.midiProgram);
        m.enabled = (nspcChannel < kMaxNspcChannels);
        m.targetNspcChannel = (nspcChannel < kMaxNspcChannels) ? nspcChannel : -1;
        m.instrumentSource.kind = MidiInstrumentSource::Kind::CreateBlank;
        m.transposeOctaves = 0;
        mappings.push_back(std::move(m));

        if (nspcChannel < kMaxNspcChannels) ++nspcChannel;
    }

    return mappings;
}

std::expected<MidiImportPreview, std::string>
analyzeMidiFileForSongSlot(const NspcProject& baseProject, const std::filesystem::path& midiPath,
                           int targetSongIndex, const MidiImportOptions& options) {
    if (targetSongIndex < 0 || targetSongIndex >= static_cast<int>(baseProject.songs().size())) {
        return std::unexpected("Target song index is out of range");
    }

    auto parsed = loadMidiFile(midiPath);
    if (!parsed.has_value()) {
        return std::unexpected(parsed.error());
    }

    const auto analysis = analyzeChannels(*parsed);

    // Count active and selected channels
    int activeChannelCount = 0;
    int selectedChannelCount = 0;
    for (int ch = 0; ch < 16; ++ch) {
        if (!analysis[static_cast<size_t>(ch)].hasNotes) continue;
        ++activeChannelCount;
    }
    for (const auto& mapping : options.channelMappings) {
        if (mapping.enabled) ++selectedChannelCount;
    }
    if (options.channelMappings.empty()) {
        selectedChannelCount = std::min(activeChannelCount, kMaxNspcChannels);
    }

    // Estimate pattern and track counts
    const int patternLengthMidiTicks = kPatternLengthNspcTicks * parsed->ppq / kNspcTicksPerQuarter;
    const int patternCount =
        std::max(1, static_cast<int>(std::ceil(static_cast<double>(parsed->totalLengthTicks) / patternLengthMidiTicks)));
    const int estimatedTrackCount = patternCount * selectedChannelCount;

    // Count new instruments/samples to be created
    int newInstrumentCount = 0;
    int newSampleCount = 0;
    uint32_t estimatedSampleBytes = 0;

    WarningCollector warnings;

    for (const auto& mapping : options.channelMappings) {
        if (!mapping.enabled) continue;
        if (mapping.instrumentSource.kind == MidiInstrumentSource::Kind::MapToExisting) continue;

        ++newInstrumentCount;

        if (mapping.instrumentSource.kind == MidiInstrumentSource::Kind::FromBrrFile &&
            mapping.instrumentSource.assetPath.has_value()) {
            auto brrData = loadBrrFile(*mapping.instrumentSource.assetPath);
            if (brrData.has_value()) {
                estimatedSampleBytes += static_cast<uint32_t>(brrData->size());
                ++newSampleCount;
            } else {
                warnings.add(std::format("Cannot estimate BRR size for channel {}: {}",
                                         mapping.midiChannel + 1, brrData.error()));
            }
        } else if (mapping.instrumentSource.kind == MidiInstrumentSource::Kind::FromNtiFile &&
                   mapping.instrumentSource.assetPath.has_value()) {
            auto ntiData = loadNtiFile(*mapping.instrumentSource.assetPath);
            if (ntiData.has_value()) {
                estimatedSampleBytes += static_cast<uint32_t>(ntiData->sample.data.size());
                ++newSampleCount;
            } else {
                warnings.add(std::format("Cannot estimate NTI size for channel {}: {}",
                                         mapping.midiChannel + 1, ntiData.error()));
            }
        } else {
            // CreateBlank: minimal silent sample (9 bytes)
            estimatedSampleBytes += 9;
            ++newSampleCount;
        }
    }

    // Check for out-of-range notes
    for (const auto& mapping : options.channelMappings) {
        if (!mapping.enabled) continue;
        const int ch = mapping.midiChannel;
        if (ch < 0 || ch >= 16 || !analysis[static_cast<size_t>(ch)].hasNotes) continue;

        const auto& chAnalysis = analysis[static_cast<size_t>(ch)];
        const int minPitch = chAnalysis.minNote - 24 + (mapping.transposeOctaves * 12);
        const int maxPitch = chAnalysis.maxNote - 24 + (mapping.transposeOctaves * 12);
        if (minPitch < 0 || maxPitch > 0x47) {
            warnings.add(std::format("Channel {} notes extend outside NSPC range (C1-B6); some notes will be clamped",
                                     ch + 1));
        }
    }

    if (activeChannelCount > kMaxNspcChannels) {
        warnings.add(std::format("MIDI file has {} active channels; SNES supports max {}",
                                 activeChannelCount, kMaxNspcChannels));
    }

    // Build channel preview data
    std::vector<MidiChannelPreview> channelPreviews;
    for (int ch = 0; ch < 16; ++ch) {
        if (!analysis[static_cast<size_t>(ch)].hasNotes) continue;
        const auto& a = analysis[static_cast<size_t>(ch)];
        MidiChannelPreview cp;
        cp.midiChannel = ch;
        cp.midiProgram = a.lastProgram;
        cp.label = formatChannelLabel(ch, a.lastProgram);
        cp.noteCount = a.noteCount;
        cp.minNote = a.minNote;
        cp.maxNote = a.maxNote;
        cp.hasVelocityChanges = a.hasVelocityChanges;
        cp.hasPanChanges = a.hasPanChanges;
        channelPreviews.push_back(std::move(cp));
    }

    MidiImportPreview preview;
    preview.fileName = parsed->fileName;
    preview.midiFormat = parsed->format;
    preview.ppq = parsed->ppq;
    preview.totalTracks = parsed->trackCount;
    preview.activeChannelCount = activeChannelCount;
    preview.selectedChannelCount = selectedChannelCount;
    preview.estimatedPatternCount = patternCount;
    preview.estimatedTrackCount = estimatedTrackCount;
    preview.estimatedNewInstrumentCount = newInstrumentCount;
    preview.estimatedNewSampleCount = newSampleCount;
    preview.currentFreeAramBytes = baseProject.aramUsage().freeBytes;
    preview.freeAramAfterDeletionBytes = estimateFreeAfterDeletion(baseProject, options);
    preview.estimatedRequiredSampleBytes = estimatedSampleBytes;
    preview.channels = std::move(channelPreviews);
    preview.warnings = std::move(warnings.warnings);

    return preview;
}

std::expected<std::pair<NspcProject, MidiImportResult>, std::string>
importMidiFileIntoSongSlot(const NspcProject& baseProject, const std::filesystem::path& midiPath,
                           int targetSongIndex, const MidiImportOptions& options) {
    if (targetSongIndex < 0 || targetSongIndex >= static_cast<int>(baseProject.songs().size())) {
        return std::unexpected("Target song index is out of range");
    }

    auto parsed = loadMidiFile(midiPath);
    if (!parsed.has_value()) {
        return std::unexpected(parsed.error());
    }

    WarningCollector warnings;
    NspcProject targetProject = baseProject;

    // Convert MIDI to NSPC structures
    auto conversionResult = convertMidiToNspc(*parsed, options, baseProject, warnings);
    if (!conversionResult.has_value()) {
        return std::unexpected(conversionResult.error());
    }

    // Build a temporary source project containing the converted data
    NspcProject sourceProject = targetProject;
    sourceProject.songs().clear();
    sourceProject.instruments().clear();
    sourceProject.samples().clear();

    sourceProject.instruments() = std::move(conversionResult->instruments);
    sourceProject.samples() = std::move(conversionResult->samples);
    sourceProject.songs().push_back(std::move(conversionResult->song));
    sourceProject.refreshAramUsage();

    // Build instrument mappings for portSong
    std::vector<InstrumentMapping> instrumentMappings;

    // For MapToExisting channels, create direct mappings
    // For created instruments, use Copy with CopyNew
    int createdInstrIndex = 0;
    for (const auto& mapping : options.channelMappings) {
        if (!mapping.enabled) continue;

        if (mapping.instrumentSource.kind == MidiInstrumentSource::Kind::MapToExisting) {
            // Find the instrument ID used in the source song for this channel
            // The NSPC channel assignment tells us which instrument index was used
            InstrumentMapping im;
            im.sourceInstrumentId = mapping.instrumentSource.existingInstrumentId;
            im.action = InstrumentMapping::Action::MapToExisting;
            im.targetInstrumentId = mapping.instrumentSource.existingInstrumentId;
            instrumentMappings.push_back(im);
        } else {
            if (createdInstrIndex < static_cast<int>(sourceProject.instruments().size())) {
                InstrumentMapping im;
                im.sourceInstrumentId = sourceProject.instruments()[static_cast<size_t>(createdInstrIndex)].id;
                im.action = InstrumentMapping::Action::Copy;
                im.sampleAction = InstrumentMapping::SampleAction::CopyNew;
                instrumentMappings.push_back(im);
                ++createdInstrIndex;
            }
        }
    }

    // If no explicit mappings, use default mapping
    if (instrumentMappings.empty() && !sourceProject.instruments().empty()) {
        instrumentMappings = buildDefaultMappings(sourceProject, targetProject, 0);
        for (auto& m : instrumentMappings) {
            m.sampleAction = InstrumentMapping::SampleAction::CopyNew;
        }
    }

    SongPortRequest request{};
    request.sourceSongIndex = 0;
    request.targetSongIndex = targetSongIndex;
    request.instrumentMappings = std::move(instrumentMappings);
    request.instrumentsToDelete = options.instrumentsToDelete;
    request.samplesToDelete = options.samplesToDelete;

    SongPortResult portResult = portSong(sourceProject, targetProject, request);
    if (!portResult.success) {
        return std::unexpected(std::format("MIDI import failed while porting song: {}", portResult.error));
    }
    targetProject.refreshAramUsage();

    MidiImportResult result;
    result.targetSongIndex = targetSongIndex;
    result.importedInstrumentCount = static_cast<int>(sourceProject.instruments().size());
    result.importedSampleCount = static_cast<int>(sourceProject.samples().size());
    result.importedPatternCount = static_cast<int>(sourceProject.songs().front().patterns().size());
    result.importedTrackCount = static_cast<int>(sourceProject.songs().front().tracks().size());
    result.warnings = std::move(warnings.warnings);

    return std::pair{std::move(targetProject), std::move(result)};
}

}  // namespace ntrak::nspc
