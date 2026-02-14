// ntrak/nspc/NspcOptimize.cpp
#include "ntrak/nspc/NspcOptimize.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>  // boyer_moore_horspool_searcher
#include <format>
#include <limits>
#include <unordered_map>
#include <vector>

namespace ntrak::nspc {
namespace {

// -----------------------------
// Tuning knobs
// -----------------------------
constexpr int kDefaultMaxOptimizeIterations = 128;     // rebuild SAM + apply best each pass
constexpr int kDefaultTopKCandidatesFromSam = 2048;    // how many SAM states we keep per pass
constexpr uint32_t kDefaultMaxCandidateBytes = 2048;   // avoid extremely large bodies (speed/quality knob)

// Subroutine call encoding cost in your compiler: opcode + u16 addr + u8 count
constexpr uint32_t kCallBytes = 4;
// Subroutine terminator (End{}) encoding cost: 0x00
constexpr uint32_t kSubTerminatorBytes = 1;
// Runtime-cost proxy: single-iteration calls are expensive for engine dispatch.
// Penalize them in savings scoring to avoid over-fragmenting streams into many count=1 calls.
constexpr int64_t kDefaultSingleIterationCallPenaltyBytes = 4;

struct EffectiveOptimizerOptions {
    int maxOptimizeIterations = kDefaultMaxOptimizeIterations;
    int topCandidatesFromSam = kDefaultTopKCandidatesFromSam;
    uint32_t maxCandidateBytes = kDefaultMaxCandidateBytes;
    int64_t singleIterationCallPenaltyBytes = kDefaultSingleIterationCallPenaltyBytes;
    bool allowSingleIterationCalls = true;
};

[[nodiscard]] EffectiveOptimizerOptions makeEffectiveOptions(const NspcOptimizerOptions& options) {
    EffectiveOptimizerOptions effective{};
    effective.maxOptimizeIterations = std::clamp(options.maxOptimizeIterations, 1, 4096);
    effective.topCandidatesFromSam = std::clamp(options.topCandidatesFromSam, 1, 16384);
    effective.maxCandidateBytes = static_cast<uint32_t>(std::clamp<int>(
        static_cast<int>(options.maxCandidateBytes), 8, 32768));
    effective.singleIterationCallPenaltyBytes =
        static_cast<int64_t>(std::clamp(options.singleIterationCallPenaltyBytes, 0, 256));
    effective.allowSingleIterationCalls = options.allowSingleIterationCalls;
    return effective;
}

// -----------------------------
// Helpers: detect / size events like your compiler
// (Copied to match NspcCompile.cpp sizing, so scoring reflects emitted bytes.)
// -----------------------------
static bool isEndEvent(const NspcEventEntry& e) {
    return std::holds_alternative<End>(e.event);
}

static bool isSubroutineCallEvent(const NspcEventEntry& e) {
    const auto* v = std::get_if<Vcmd>(&e.event);
    if (!v) {
        return false;
    }
    return std::holds_alternative<VcmdSubroutineCall>(v->vcmd);
}

static bool isPitchSlideToNoteEvent(const NspcEventEntry& e) {
    const auto* v = std::get_if<Vcmd>(&e.event);
    if (!v) {
        return false;
    }
    return std::holds_alternative<VcmdPitchSlideToNote>(v->vcmd);
}

static bool isDurationEvent(const NspcEventEntry& e) {
    return std::holds_alternative<Duration>(e.event);
}

static bool isDurationWithoutQv(const NspcEventEntry& e) {
    const auto* duration = std::get_if<Duration>(&e.event);
    if (!duration) {
        return false;
    }
    return !duration->quantization.has_value() && !duration->velocity.has_value();
}

static bool consumesDurationTicks(const NspcEventEntry& e) {
    return std::holds_alternative<Note>(e.event) || std::holds_alternative<Tie>(e.event) ||
           std::holds_alternative<Rest>(e.event) || std::holds_alternative<Percussion>(e.event);
}

static bool sliceConsumesDurationTicks(const std::vector<NspcEventEntry>& events, size_t startEventIndex,
                                       size_t eventCount) {
    if (startEventIndex >= events.size()) {
        return false;
    }
    const size_t end = std::min(events.size(), startEventIndex + eventCount);
    for (size_t i = startEventIndex; i < end; ++i) {
        if (consumesDurationTicks(events[i])) {
            return true;
        }
    }
    return false;
}

static uint32_t vcmdEncodedSize(const Vcmd& value) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) { return 0u; },
            [](const VcmdInst&) { return 2u; },
            [](const VcmdPanning&) { return 2u; },
            [](const VcmdPanFade&) { return 3u; },
            [](const VcmdVibratoOn&) { return 4u; },
            [](const VcmdVibratoOff&) { return 1u; },
            [](const VcmdGlobalVolume&) { return 2u; },
            [](const VcmdGlobalVolumeFade&) { return 3u; },
            [](const VcmdTempo&) { return 2u; },
            [](const VcmdTempoFade&) { return 3u; },
            [](const VcmdGlobalTranspose&) { return 2u; },
            [](const VcmdPerVoiceTranspose&) { return 2u; },
            [](const VcmdTremoloOn&) { return 4u; },
            [](const VcmdTremoloOff&) { return 1u; },
            [](const VcmdVolume&) { return 2u; },
            [](const VcmdVolumeFade&) { return 3u; },
            [](const VcmdSubroutineCall&) { return 4u; },
            [](const VcmdVibratoFadeIn&) { return 2u; },
            [](const VcmdPitchEnvelopeTo&) { return 4u; },
            [](const VcmdPitchEnvelopeFrom&) { return 4u; },
            [](const VcmdPitchEnvelopeOff&) { return 1u; },
            [](const VcmdFineTune&) { return 2u; },
            [](const VcmdEchoOn&) { return 4u; },
            [](const VcmdEchoOff&) { return 1u; },
            [](const VcmdEchoParams&) { return 4u; },
            [](const VcmdEchoVolumeFade&) { return 4u; },
            [](const VcmdPitchSlideToNote&) { return 4u; },
            [](const VcmdPercussionBaseInstrument&) { return 2u; },
            [](const VcmdNOP&) { return 3u; },
            [](const VcmdMuteChannel&) { return 1u; },
            [](const VcmdFastForwardOn&) { return 1u; },
            [](const VcmdFastForwardOff&) { return 1u; },
            [](const VcmdUnused&) { return 1u; },
            [](const VcmdExtension& value) { return static_cast<uint32_t>(1u + value.paramCount); },
        },
        value.vcmd);
}

static uint32_t eventEncodedSize(const NspcEventEntry& entry) {
    return std::visit(
        nspc::overloaded{
            [](const std::monostate&) { return 0u; },
            [](const Duration& value) {
                return (value.quantization.has_value() || value.velocity.has_value()) ? 2u : 1u;
            },
            [](const Vcmd& value) { return vcmdEncodedSize(value); },
            [](const Note&) { return 1u; },
            [](const Tie&) { return 1u; },
            [](const Rest&) { return 1u; },
            [](const Percussion&) { return 1u; },
            [](const Subroutine&) { return 0u; }, // ignored by compiler
            [](const End&) { return 1u; },
        },
        entry.event);
}

// Next id: mirror the scan used in your NspcData.cpp internal helper.
static NspcEventId nextEventIdForSong(const NspcSong& song) {
    NspcEventId next = 1;
    for (const auto& t : song.tracks()) {
        for (const auto& e : t.events) {
            next = std::max(next, e.id + 1);
        }
    }
    for (const auto& s : song.subroutines()) {
        for (const auto& e : s.events) {
            next = std::max(next, e.id + 1);
        }
    }
    return next;
}

// -----------------------------
// Stable-ish 64-bit hashing for semantic token keys
// We ensure top-bit=0 so we can use top-bit=1 for unique separators.
// -----------------------------
static uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static void hashAdd(uint64_t& h, uint64_t v) {
    // Cheap combine
    h ^= splitmix64(v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2));
}

static uint64_t hashVcmdSemantic(const Vcmd& vc) {
    uint64_t h = 0xC0DEC0DE12345678ull;

    std::visit(
        nspc::overloaded{
            [&](const std::monostate&) { hashAdd(h, 0x00); },

            [&](const VcmdInst& v) {
                hashAdd(h, 0xE0);
                hashAdd(h, v.instrumentIndex);
            },
            [&](const VcmdPanning& v) {
                hashAdd(h, 0xE1);
                hashAdd(h, v.panning);
            },
            [&](const VcmdPanFade& v) {
                hashAdd(h, 0xE2);
                hashAdd(h, v.time);
                hashAdd(h, v.target);
            },
            [&](const VcmdVibratoOn& v) {
                hashAdd(h, 0xE3);
                hashAdd(h, v.delay);
                hashAdd(h, v.rate);
                hashAdd(h, v.depth);
            },
            [&](const VcmdVibratoOff&) { hashAdd(h, 0xE4); },

            [&](const VcmdGlobalVolume& v) {
                hashAdd(h, 0xE5);
                hashAdd(h, v.volume);
            },
            [&](const VcmdGlobalVolumeFade& v) {
                hashAdd(h, 0xE6);
                hashAdd(h, v.time);
                hashAdd(h, v.target);
            },
            [&](const VcmdTempo& v) {
                hashAdd(h, 0xE7);
                hashAdd(h, v.tempo);
            },
            [&](const VcmdTempoFade& v) {
                hashAdd(h, 0xE8);
                hashAdd(h, v.time);
                hashAdd(h, v.target);
            },
            [&](const VcmdGlobalTranspose& v) {
                hashAdd(h, 0xE9);
                hashAdd(h, static_cast<uint8_t>(v.semitones));
            },
            [&](const VcmdPerVoiceTranspose& v) {
                hashAdd(h, 0xEA);
                hashAdd(h, static_cast<uint8_t>(v.semitones));
            },
            [&](const VcmdTremoloOn& v) {
                hashAdd(h, 0xEB);
                hashAdd(h, v.delay);
                hashAdd(h, v.rate);
                hashAdd(h, v.depth);
            },
            [&](const VcmdTremoloOff&) { hashAdd(h, 0xEC); },

            [&](const VcmdVolume& v) {
                hashAdd(h, 0xED);
                hashAdd(h, v.volume);
            },
            [&](const VcmdVolumeFade& v) {
                hashAdd(h, 0xEE);
                hashAdd(h, v.time);
                hashAdd(h, v.target);
            },

            // Calls are treated as boundaries (we don't want nesting), but keep hashing defined.
            [&](const VcmdSubroutineCall& v) {
                hashAdd(h, 0xEF);
                hashAdd(h, static_cast<uint64_t>(static_cast<uint32_t>(v.subroutineId)));
                hashAdd(h, v.count);
            },

            [&](const VcmdVibratoFadeIn& v) {
                hashAdd(h, 0xF0);
                hashAdd(h, v.time);
            },
            [&](const VcmdPitchEnvelopeTo& v) {
                hashAdd(h, 0xF1);
                hashAdd(h, v.delay);
                hashAdd(h, v.length);
                hashAdd(h, v.semitone);
            },
            [&](const VcmdPitchEnvelopeFrom& v) {
                hashAdd(h, 0xF2);
                hashAdd(h, v.delay);
                hashAdd(h, v.length);
                hashAdd(h, v.semitone);
            },
            [&](const VcmdPitchEnvelopeOff&) { hashAdd(h, 0xF3); },
            [&](const VcmdFineTune& v) {
                hashAdd(h, 0xF4);
                hashAdd(h, static_cast<uint8_t>(v.semitones));
            },
            [&](const VcmdEchoOn& v) {
                hashAdd(h, 0xF5);
                hashAdd(h, v.channels);
                hashAdd(h, v.left);
                hashAdd(h, v.right);
            },
            [&](const VcmdEchoOff&) { hashAdd(h, 0xF6); },
            [&](const VcmdEchoParams& v) {
                hashAdd(h, 0xF7);
                hashAdd(h, v.delay);
                hashAdd(h, v.feedback);
                hashAdd(h, v.firIndex);
            },
            [&](const VcmdEchoVolumeFade& v) {
                hashAdd(h, 0xF8);
                hashAdd(h, v.time);
                hashAdd(h, v.leftTarget);
                hashAdd(h, v.rightTarget);
            },
            [&](const VcmdPitchSlideToNote& v) {
                hashAdd(h, 0xF9);
                hashAdd(h, v.delay);
                hashAdd(h, v.length);
                hashAdd(h, v.note);
            },
            [&](const VcmdPercussionBaseInstrument& v) {
                hashAdd(h, 0xFA);
                hashAdd(h, v.index);
            },
            [&](const VcmdNOP& v) {
                hashAdd(h, 0xFB);
                hashAdd(h, v.nopBytes);
            },
            [&](const VcmdMuteChannel&) { hashAdd(h, 0xFC); },
            [&](const VcmdFastForwardOn&) { hashAdd(h, 0xFD); },
            [&](const VcmdFastForwardOff&) { hashAdd(h, 0xFE); },
            [&](const VcmdUnused&) { hashAdd(h, 0xFF); },
            [&](const VcmdExtension& v) {
                hashAdd(h, 0xF0FF);
                hashAdd(h, v.id);
                hashAdd(h, v.paramCount);
                for (uint8_t i = 0; i < v.paramCount; ++i) {
                    hashAdd(h, v.params[i]);
                }
            },
        },
        vc.vcmd);

    // Keep top-bit clear (reserved for separators).
    h &= ~(1ull << 63);
    return h;
}

static uint64_t hashEventSemantic(const NspcEventEntry& e) {
    uint64_t h = 0xBADC0FFEE0DDF00Dull;

    std::visit(
        nspc::overloaded{
            [&](const std::monostate&) { hashAdd(h, 0x00); },

            [&](const Duration& v) {
                // Canonicalize like encoder:
                uint8_t ticks = v.ticks;
                if (ticks == 0) {
                    ticks = 1;
                }
                hashAdd(h, 0x01);
                hashAdd(h, ticks);

                if (v.quantization.has_value() || v.velocity.has_value()) {
                    const uint8_t q = static_cast<uint8_t>(v.quantization.value_or(0) & 0x07);
                    const uint8_t vel = static_cast<uint8_t>(v.velocity.value_or(0) & 0x0F);
                    hashAdd(h, 0x100);
                    hashAdd(h, q);
                    hashAdd(h, vel);
                } else {
                    hashAdd(h, 0x101);
                }
            },

            [&](const Vcmd& v) {
                hashAdd(h, 0x02);
                hashAdd(h, hashVcmdSemantic(v));
            },

            [&](const Note& v) {
                hashAdd(h, 0x03);
                hashAdd(h, v.pitch);
            },
            [&](const Tie&) { hashAdd(h, 0x04); },
            [&](const Rest&) { hashAdd(h, 0x05); },
            [&](const Percussion& v) {
                hashAdd(h, 0x06);
                hashAdd(h, v.index);
            },

            // Not encodable; treat as a hard boundary in tokenization, but hashing defined anyway.
            [&](const Subroutine& v) {
                hashAdd(h, 0x07);
                hashAdd(h, static_cast<uint64_t>(static_cast<uint32_t>(v.id)));
                hashAdd(h, v.originalAddr);
            },

            // IMPORTANT: End must never be inside extracted subroutines.
            // We never emit End tokens into the SAM domain; hashing defined anyway.
            [&](const End&) { hashAdd(h, 0x08); },
        },
        e.event);

    h = splitmix64(h);
    h &= ~(1ull << 63);
    return h;
}

// -----------------------------
// Segment building: exclude End (0x00) from match domain and split at boundaries.
// Boundaries:
// - End{}
// - non-encodable events (encoded size == 0)
// - VcmdSubroutineCall (we avoid nesting calls)
// -----------------------------
struct Segment {
    int trackIndex = -1;
    size_t eventStartIndex = 0; // index in track.events corresponding to tokens[0]
    std::vector<uint64_t> tokens;
    std::vector<uint8_t> sizes;
};

struct GlobalPosInfo {
    int segmentIndex = -1;
    uint32_t posInSegment = 0;
};

static void flushSegmentIfNonEmpty(std::vector<Segment>& segs, Segment& current) {
    if (!current.tokens.empty()) {
        segs.push_back(std::move(current));
        current = Segment{};
    }
}

static std::vector<Segment> buildSegmentsFromTracks(const std::vector<NspcTrack>& tracks) {
    std::vector<Segment> segs;
    segs.reserve(tracks.size() * 2);

    for (int ti = 0; ti < static_cast<int>(tracks.size()); ++ti) {
        const auto& t = tracks[static_cast<size_t>(ti)];

        Segment cur;
        cur.trackIndex = ti;
        cur.eventStartIndex = 0;

        bool started = false;
        size_t segStart = 0;

        for (size_t i = 0; i < t.events.size(); ++i) {
            const auto& e = t.events[i];

            if (isEndEvent(e)) {
                // End is a hard stop and must NOT be included in subroutine bodies.
                flushSegmentIfNonEmpty(segs, cur);
                break;
            }

            // Boundary: avoid nesting calls and avoid spanning non-encodable markers.
            if (isSubroutineCallEvent(e) || eventEncodedSize(e) == 0) {
                flushSegmentIfNonEmpty(segs, cur);
                // next segment starts after this boundary event
                started = false;
                continue;
            }

            if (!started) {
                started = true;
                segStart = i;
                cur.trackIndex = ti;
                cur.eventStartIndex = segStart;
                cur.tokens.clear();
                cur.sizes.clear();
            }

            cur.tokens.push_back(hashEventSemantic(e));
            cur.sizes.push_back(static_cast<uint8_t>(eventEncodedSize(e)));
        }

        flushSegmentIfNonEmpty(segs, cur);
    }

    return segs;
}

// -----------------------------
// Suffix Automaton over uint64_t symbols (event tokens + unique separators)
// Using vector transitions (small out-degree typical), faster than unordered_map in practice here.
// -----------------------------
struct SamTrans {
    uint64_t sym = 0;
    int next = -1;
};

struct SamState {
    int link = -1;
    int len = 0;         // max length
    int firstPos = -1;   // end position of one occurrence
    int occ = 0;         // endpos count (after propagation)
    std::vector<SamTrans> next;
};

static int samFindNext(const SamState& st, uint64_t sym) {
    for (const auto& tr : st.next) {
        if (tr.sym == sym) {
            return tr.next;
        }
    }
    return -1;
}

static void samSetNext(SamState& st, uint64_t sym, int nxt) {
    for (auto& tr : st.next) {
        if (tr.sym == sym) {
            tr.next = nxt;
            return;
        }
    }
    st.next.push_back(SamTrans{sym, nxt});
}

class SuffixAutomaton {
public:
    explicit SuffixAutomaton(size_t reserveStates = 0) {
        states_.reserve(std::max<size_t>(reserveStates, 2));
        states_.push_back(SamState{}); // state 0
        states_[0].link = -1;
        last_ = 0;
    }

    void extend(uint64_t c, int pos) {
        int cur = static_cast<int>(states_.size());
        states_.push_back(SamState{});
        states_[cur].len = states_[last_].len + 1;
        states_[cur].firstPos = pos;
        states_[cur].occ = 1;

        int p = last_;
        while (p != -1 && samFindNext(states_[p], c) == -1) {
            samSetNext(states_[p], c, cur);
            p = states_[p].link;
        }

        if (p == -1) {
            states_[cur].link = 0;
        } else {
            int q = samFindNext(states_[p], c);
            if (states_[p].len + 1 == states_[q].len) {
                states_[cur].link = q;
            } else {
                int clone = static_cast<int>(states_.size());
                states_.push_back(states_[q]); // copy
                states_[clone].len = states_[p].len + 1;
                states_[clone].occ = 0; // clones don't directly represent new endpos

                while (p != -1 && samFindNext(states_[p], c) == q) {
                    samSetNext(states_[p], c, clone);
                    p = states_[p].link;
                }
                states_[q].link = clone;
                states_[cur].link = clone;
            }
        }

        last_ = cur;
    }

    void computeOccurrences() {
        int maxLen = 0;
        for (const auto& st : states_) {
            maxLen = std::max(maxLen, st.len);
        }

        std::vector<int> cnt(static_cast<size_t>(maxLen + 1), 0);
        for (const auto& st : states_) {
            cnt[static_cast<size_t>(st.len)]++;
        }
        for (int i = 1; i <= maxLen; ++i) {
            cnt[static_cast<size_t>(i)] += cnt[static_cast<size_t>(i - 1)];
        }

        std::vector<int> order(states_.size(), 0);
        for (int i = static_cast<int>(states_.size()) - 1; i >= 0; --i) {
            order[static_cast<size_t>(--cnt[static_cast<size_t>(states_[i].len)])] = i;
        }

        // propagate occ in descending len
        for (int i = static_cast<int>(order.size()) - 1; i > 0; --i) {
            int v = order[static_cast<size_t>(i)];
            int parent = states_[v].link;
            if (parent >= 0) {
                states_[static_cast<size_t>(parent)].occ += states_[static_cast<size_t>(v)].occ;
            }
        }
    }

    const std::vector<SamState>& states() const { return states_; }

private:
    std::vector<SamState> states_;
    int last_ = 0;
};

// -----------------------------
// Candidate selection + application
// -----------------------------
struct Candidate {
    int stateIndex = -1;
    int lenTok = 0;
    uint32_t lenBytes = 0;
    int occ = 0;
    int firstPos = -1;
    int64_t estSavings = 0;
};

struct Run {
    size_t startEventIndex = 0;
    uint32_t repeats = 0; // occurrences in a row (adjacent blocks)
};

struct ApplyPlan {
    int trackIndex = -1;
    std::vector<Run> runs;
};

static bool appendCallChunkIterations(uint32_t repeats, bool allowSingleIterationCalls, std::vector<uint32_t>& outChunks) {
    outChunks.clear();
    if (repeats < (allowSingleIterationCalls ? 1u : 2u)) {
        return false;
    }

    uint32_t remaining = repeats;
    while (remaining > 0) {
        // Count encodes the actual repeat count, so legal chunk range is [1, 255].
        const uint32_t chunk = std::min<uint32_t>(remaining, 255u);

        outChunks.push_back(chunk);
        remaining -= chunk;
    }

    return true;
}

static uint32_t optimisticMinCallCountForOccurrences(uint32_t occurrences) {
    if (occurrences < 1u) {
        return std::numeric_limits<uint32_t>::max();
    }
    return (occurrences + 254u) / 255u;
}

static NspcEventEntry makeCallEntry(NspcEventId& nextId, int subId, uint8_t count) {
    Vcmd v;
    v.vcmd = VcmdSubroutineCall{
        .subroutineId = subId,
        .originalAddr = 0, // compiler will patch via id->addr map
        .count = count,
    };
    NspcEventEntry e;
    e.id = nextId++;
    e.event = v;
    e.originalAddr = std::nullopt;
    return e;
}

static NspcEventEntry makeEndEntry(NspcEventId& nextId) {
    NspcEventEntry e;
    e.id = nextId++;
    e.event = End{};
    e.originalAddr = std::nullopt;
    return e;
}

static bool buildApplyPlansForCandidate(
    const Candidate& cand,
    const std::vector<NspcTrack>& tracks,
    const std::vector<Segment>& segments,
    const std::vector<uint64_t>& globalSeq,
    const std::vector<uint32_t>& prefixBytes,
    const std::vector<uint32_t>& prefixSep,
    const EffectiveOptimizerOptions& options,
    std::vector<ApplyPlan>& outPlans,
    uint32_t& outLenTok,
    uint32_t& outLenBytes,
    uint64_t& outRepresentativeTrack,
    size_t& outRepresentativeStart)
{
    outPlans.clear();

    const int lenTok = cand.lenTok;
    if (lenTok <= 0) {
        return false;
    }

    const int endPos = cand.firstPos;
    const int startPos = endPos - lenTok + 1;
    if (startPos < 0) {
        return false;
    }

    // Ensure no separator inside substring (O(1) using prefixSep)
    if (prefixSep[static_cast<size_t>(startPos + lenTok)] != prefixSep[static_cast<size_t>(startPos)]) {
        return false;
    }

    const uint32_t lenBytes =
        prefixBytes[static_cast<size_t>(startPos + lenTok)] - prefixBytes[static_cast<size_t>(startPos)];
    if (lenBytes == 0 || lenBytes > options.maxCandidateBytes) {
        return false;
    }

    const auto patternBegin = globalSeq.begin() + startPos;
    const auto patternEnd = patternBegin + lenTok;
    const size_t patternSize = static_cast<size_t>(lenTok);

    // Gather match starts per track (across all segments)
    // Track count is unknown here; we’ll discover max track index.
    int maxTrack = -1;
    for (const auto& seg : segments) {
        maxTrack = std::max(maxTrack, seg.trackIndex);
    }
    if (maxTrack < 0) {
        return false;
    }

    std::vector<std::vector<size_t>> startsByTrack(static_cast<size_t>(maxTrack + 1));

    const auto searcher = std::boyer_moore_horspool_searcher(patternBegin, patternEnd);
    std::optional<bool> candidateEndsWithBareDuration;

    for (const auto& seg : segments) {
        if (seg.tokens.size() < patternSize) {
            continue;
        }

        auto it = std::search(seg.tokens.begin(), seg.tokens.end(), searcher);
        while (it != seg.tokens.end()) {
            const size_t pos = static_cast<size_t>(it - seg.tokens.begin());
            const size_t startEventIndex = seg.eventStartIndex + pos;

            if (seg.trackIndex >= 0 && static_cast<size_t>(seg.trackIndex) < tracks.size()) {
                const auto& trackEvents = tracks[static_cast<size_t>(seg.trackIndex)].events;
                // Legacy safety guard from mITroid optimizer:
                // don't start extracted bodies on F9, and don't end them immediately before F9.
                // This avoids fragile call boundaries around pitch-slide commands.
                if (startEventIndex < trackEvents.size() && isPitchSlideToNoteEvent(trackEvents[startEventIndex])) {
                    it = std::search(it + 1, seg.tokens.end(), searcher);
                    continue;
                }
                const size_t endEventIndex = startEventIndex + patternSize;
                if (endEventIndex < trackEvents.size() && isPitchSlideToNoteEvent(trackEvents[endEventIndex])) {
                    it = std::search(it + 1, seg.tokens.end(), searcher);
                    continue;
                }

                // Conservative safety rule: avoid placing a subroutine call directly after a Duration event.
                // This preserves Duration-byte adjacency in caller streams and prevents Duration->Call boundaries.
                if (startEventIndex > 0 && startEventIndex - 1 < trackEvents.size() &&
                    isDurationEvent(trackEvents[startEventIndex - 1])) {
                    it = std::search(it + 1, seg.tokens.end(), searcher);
                    continue;
                }

                // Engine safety rule: don't extract bodies that end with a bare Duration byte.
                // At subroutine end, that trailing Duration can be ambiguously interpreted against
                // the terminator stream and desynchronize playback on some engines.
                if (endEventIndex == 0 || endEventIndex > trackEvents.size()) {
                    it = std::search(it + 1, seg.tokens.end(), searcher);
                    continue;
                }
                if (!candidateEndsWithBareDuration.has_value()) {
                    candidateEndsWithBareDuration = isDurationWithoutQv(trackEvents[endEventIndex - 1]);
                    if (*candidateEndsWithBareDuration) {
                        return false;
                    }
                }
            }

            startsByTrack[static_cast<size_t>(seg.trackIndex)].push_back(startEventIndex);

            // allow overlaps in detection; we’ll drop overlaps in planning
            it = std::search(it + 1, seg.tokens.end(), searcher);
        }
    }

    // Build runs (non-overlapping, grouped adjacency)
    std::vector<ApplyPlan> plans;
    plans.reserve(startsByTrack.size());

    uint64_t repTrack = std::numeric_limits<uint64_t>::max();
    size_t repStart = 0;
    bool haveRep = false;

    uint64_t totalOccurrences = 0;
    uint64_t totalCallCount = 0;
    uint64_t totalSingleIterationCalls = 0;
    std::optional<bool> candidateConsumesDuration;
    std::vector<uint32_t> callChunks;
    callChunks.reserve(8);

    for (int ti = 0; ti <= maxTrack; ++ti) {
        auto& starts = startsByTrack[static_cast<size_t>(ti)];
        if (starts.empty()) {
            continue;
        }

        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

        ApplyPlan plan;
        plan.trackIndex = ti;
        const auto& trackEvents = tracks[static_cast<size_t>(ti)].events;

        size_t i = 0;
        size_t nextAllowed = 0;
        while (i < starts.size()) {
            const size_t s = starts[i];

            if (s < nextAllowed) {
                ++i;
                continue;
            }

            // Accept this start; now grow an adjacency run: s, s+lenTok, s+2lenTok, ...
            uint32_t repeats = 1;
            size_t j = i + 1;
            while (j < starts.size() && starts[j] == s + static_cast<size_t>(repeats) * static_cast<size_t>(lenTok)) {
                ++repeats;
                ++j;
            }

            // Single-run calls are the riskiest for runtime dispatch load. Keep them only
            // when the extracted body advances musical time (contains Note/Tie/Rest/Percussion).
            if (repeats == 1u) {
                if (!candidateConsumesDuration.has_value()) {
                    candidateConsumesDuration =
                        sliceConsumesDurationTicks(trackEvents, s, static_cast<size_t>(lenTok));
                }
                if (!*candidateConsumesDuration) {
                    ++i;
                    continue;
                }
            }

            callChunks.clear();
            if (!appendCallChunkIterations(repeats, options.allowSingleIterationCalls, callChunks)) {
                ++i;
                continue;
            }

            plan.runs.push_back(Run{.startEventIndex = s, .repeats = repeats});

            if (!haveRep) {
                haveRep = true;
                repTrack = static_cast<uint64_t>(ti);
                repStart = s;
            }

            totalOccurrences += repeats;
            totalCallCount += static_cast<uint64_t>(callChunks.size());
            for (const uint32_t chunk : callChunks) {
                if (chunk == 1u) {
                    ++totalSingleIterationCalls;
                }
            }

            nextAllowed = s + static_cast<size_t>(repeats) * static_cast<size_t>(lenTok);

            // Skip any starts that overlap the entire run
            i = j;
            while (i < starts.size() && starts[i] < nextAllowed) {
                ++i;
            }
        }

        if (!plan.runs.empty()) {
            plans.push_back(std::move(plan));
        }
    }

    if (!haveRep || totalOccurrences < 2) {
        return false;
    }

    // Real savings: removed - calls - sub body (+End)
    const uint64_t removedBytes = totalOccurrences * static_cast<uint64_t>(lenBytes);
    const uint64_t callBytes = totalCallCount * static_cast<uint64_t>(kCallBytes);
    const uint64_t subBytes = static_cast<uint64_t>(lenBytes + kSubTerminatorBytes);
    const int64_t runtimePenalty =
        static_cast<int64_t>(totalSingleIterationCalls) * options.singleIterationCallPenaltyBytes;

    const int64_t realSavings = static_cast<int64_t>(removedBytes) - static_cast<int64_t>(callBytes) -
                                static_cast<int64_t>(subBytes) - runtimePenalty;

    if (realSavings <= 0) {
        return false;
    }

    outPlans = std::move(plans);
    outLenTok = static_cast<uint32_t>(lenTok);
    outLenBytes = lenBytes;
    outRepresentativeTrack = repTrack;
    outRepresentativeStart = repStart;
    return true;
}

static void applyPlansCreateSubroutineAndRewriteTracks(
    NspcSong& song,
    const std::vector<ApplyPlan>& plans,
    uint32_t lenTok,
    uint64_t repTrack,
    size_t repStart,
    bool allowSingleIterationCalls,
    NspcEventId& nextId)
{
    auto& tracks = song.tracks();
    auto& subs = song.subroutines();

    const int newSubId = static_cast<int>(subs.size());

    // Build subroutine body from representative slice (must not contain End)
    const auto& srcTrack = tracks[static_cast<size_t>(repTrack)];
    const size_t sliceEnd = repStart + static_cast<size_t>(lenTok);

    std::vector<NspcEventEntry> subEvents;
    subEvents.reserve(static_cast<size_t>(lenTok) + 1);

    for (size_t i = repStart; i < sliceEnd; ++i) {
        const auto& src = srcTrack.events[i];
        // Safety: ensure no End is included
        if (std::holds_alternative<End>(src.event)) {
            break;
        }
        NspcEventEntry cloned = src;
        cloned.id = nextId++;
        cloned.originalAddr = std::nullopt;
        subEvents.push_back(std::move(cloned));
    }

    // Append required terminator
    subEvents.push_back(makeEndEntry(nextId));

    NspcSubroutine sub;
    sub.id = newSubId;
    sub.originalAddr = 0;
    sub.events = std::move(subEvents);

    subs.push_back(std::move(sub));

    // Rewrite each track according to runs
    for (const auto& plan : plans) {
        auto& t = tracks[static_cast<size_t>(plan.trackIndex)];
        const auto old = t.events;

        std::vector<NspcEventEntry> out;
        out.reserve(old.size());

        size_t runIdx = 0;
        size_t i = 0;
        std::vector<uint32_t> callChunks;
        callChunks.reserve(8);

        while (i < old.size()) {
            // Stop on End, but copy it through
            if (std::holds_alternative<End>(old[i].event)) {
                out.push_back(old[i]);
                break;
            }

            if (runIdx < plan.runs.size() && i == plan.runs[runIdx].startEventIndex) {
                const Run& r = plan.runs[runIdx];

                // Emit calls for repeats (chunked to 1..255 iterations per call).
                callChunks.clear();
                if (appendCallChunkIterations(r.repeats, allowSingleIterationCalls, callChunks)) {
                    for (const uint32_t chunk : callChunks) {
                        const uint8_t count = static_cast<uint8_t>(chunk);
                        out.push_back(makeCallEntry(nextId, newSubId, count));
                    }
                } else {
                    // Defensive fallback: keep original events if run encoding is impossible.
                    for (size_t keep = 0; keep < static_cast<size_t>(r.repeats) * static_cast<size_t>(lenTok); ++keep) {
                        out.push_back(old[i + keep]);
                    }
                }

                // Skip r.repeats * lenTok events
                i += static_cast<size_t>(r.repeats) * static_cast<size_t>(lenTok);
                ++runIdx;
                continue;
            }

            out.push_back(old[i]);
            ++i;
        }

        t.events = std::move(out);
    }
}

static bool hasAnySubroutineCalls(const std::vector<NspcTrack>& tracks) {
    for (const auto& t : tracks) {
        for (const auto& e : t.events) {
            if (isSubroutineCallEvent(e)) {
                return true;
            }
        }
    }
    return false;
}

static void buildGlobalSequenceWithSeparators(
    const std::vector<Segment>& segments,
    std::vector<uint64_t>& outSeq,
    std::vector<uint32_t>& outPrefixBytes,
    std::vector<uint32_t>& outPrefixSep)
{
    outSeq.clear();

    size_t totalTokens = 0;
    for (const auto& s : segments) {
        totalTokens += s.tokens.size() + 1; // +1 separator
    }
    outSeq.reserve(totalTokens);

    std::vector<uint8_t> sizes;
    sizes.reserve(totalTokens);

    // Unique separators: top-bit=1 and unique payload
    uint64_t sepId = 1;

    for (const auto& seg : segments) {
        for (size_t i = 0; i < seg.tokens.size(); ++i) {
            outSeq.push_back(seg.tokens[i]);
            sizes.push_back(seg.sizes[i]);
        }
        // separator token: unique and top-bit set
        outSeq.push_back((1ull << 63) | (sepId++));
        sizes.push_back(0);
    }

    outPrefixBytes.assign(outSeq.size() + 1, 0);
    outPrefixSep.assign(outSeq.size() + 1, 0);

    for (size_t i = 0; i < outSeq.size(); ++i) {
        outPrefixBytes[i + 1] = outPrefixBytes[i] + static_cast<uint32_t>(sizes[i]);
        outPrefixSep[i + 1] = outPrefixSep[i] + ((outSeq[i] & (1ull << 63)) ? 1u : 0u);
    }
}

static std::vector<Candidate> collectTopCandidatesFromSam(
    const SuffixAutomaton& sam,
    const std::vector<uint64_t>& globalSeq,
    const std::vector<uint32_t>& prefixBytes,
    const std::vector<uint32_t>& prefixSep,
    const EffectiveOptimizerOptions& options)
{
    (void)globalSeq;

    auto isBetterCandidate = [](const Candidate& a, const Candidate& b) {
        if (a.estSavings != b.estSavings) return a.estSavings > b.estSavings;
        if (a.lenBytes != b.lenBytes) return a.lenBytes > b.lenBytes;
        if (a.lenTok != b.lenTok) return a.lenTok > b.lenTok;
        if (a.occ != b.occ) return a.occ > b.occ;
        if (a.firstPos != b.firstPos) return a.firstPos < b.firstPos;
        return a.stateIndex < b.stateIndex;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(sam.states().size());

    const auto& states = sam.states();
    for (int si = 1; si < static_cast<int>(states.size()); ++si) {
        const auto& st = states[static_cast<size_t>(si)];
        if (st.occ < 2 || st.len <= 0 || st.firstPos < 0) {
            continue;
        }

        const int lenTok = st.len;
        const int endPos = st.firstPos;
        const int startPos = endPos - lenTok + 1;
        if (startPos < 0) {
            continue;
        }

        // Reject if substring includes any separator (O(1))
        if (prefixSep[static_cast<size_t>(startPos + lenTok)] != prefixSep[static_cast<size_t>(startPos)]) {
            continue;
        }

        const uint32_t lenBytes =
            prefixBytes[static_cast<size_t>(startPos + lenTok)] - prefixBytes[static_cast<size_t>(startPos)];
        if (lenBytes == 0 || lenBytes > options.maxCandidateBytes) {
            continue;
        }

        // Estimated savings (optimistic) with direct repeat counts:
        // calls encode 1..255 iterations, so the lower bound on call count is ceil(occ/255).
        const uint32_t optimisticCalls = optimisticMinCallCountForOccurrences(static_cast<uint32_t>(st.occ));
        if (optimisticCalls == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        const int64_t est = static_cast<int64_t>(st.occ) * static_cast<int64_t>(lenBytes) -
                            static_cast<int64_t>(optimisticCalls) * static_cast<int64_t>(kCallBytes) -
                            static_cast<int64_t>(lenBytes + kSubTerminatorBytes);

        if (est <= 0) {
            continue;
        }

        Candidate c;
        c.stateIndex = si;
        c.lenTok = lenTok;
        c.lenBytes = lenBytes;
        c.occ = st.occ;
        c.firstPos = st.firstPos;
        c.estSavings = est;

        candidates.push_back(c);
    }

    if (candidates.size() > static_cast<size_t>(options.topCandidatesFromSam)) {
        auto split = candidates.begin() + options.topCandidatesFromSam;
        std::nth_element(candidates.begin(), split, candidates.end(), isBetterCandidate);
        candidates.resize(static_cast<size_t>(options.topCandidatesFromSam));
    }

    std::sort(candidates.begin(), candidates.end(), isBetterCandidate);
    return candidates;
}

} // namespace

// -----------------------------
// Public entry point
// -----------------------------
void optimizeSongSubroutines(NspcSong& song, const NspcOptimizerOptions& options) {
    const EffectiveOptimizerOptions effective = makeEffectiveOptions(options);

    // Ensure linear tracks first (no nesting in the extraction stage)
    song.flattenSubroutines();

    // If flattening could not remove calls (recursive/missing), don't break the song.
    if (hasAnySubroutineCalls(song.tracks())) {
        return;
    }

    // Start fresh: we are going to create our own optimized set.
    song.subroutines().clear();

    NspcEventId nextId = nextEventIdForSong(song);

    for (int iter = 0; iter < effective.maxOptimizeIterations; ++iter) {
        // Build match domain segments (excluding End and splitting at boundaries)
        const std::vector<Segment> segments = buildSegmentsFromTracks(song.tracks());

        // If nothing meaningful, stop
        size_t tokenCount = 0;
        for (const auto& s : segments) tokenCount += s.tokens.size();
        if (tokenCount < 8) {
            break;
        }

        // Build global seq + prefix sums
        std::vector<uint64_t> globalSeq;
        std::vector<uint32_t> prefixBytes;
        std::vector<uint32_t> prefixSep;
        buildGlobalSequenceWithSeparators(segments, globalSeq, prefixBytes, prefixSep);

        // Build SAM
        SuffixAutomaton sam(globalSeq.size() * 2);
        for (int i = 0; i < static_cast<int>(globalSeq.size()); ++i) {
            sam.extend(globalSeq[static_cast<size_t>(i)], i);
        }
        sam.computeOccurrences();

        // Candidates
        const std::vector<Candidate> candidates =
            collectTopCandidatesFromSam(sam, globalSeq, prefixBytes, prefixSep, effective);
        if (candidates.empty()) {
            break;
        }

        bool applied = false;

        // Try best-first; apply the first candidate that yields real positive savings after overlap/run handling.
        for (const auto& cand : candidates) {
            std::vector<ApplyPlan> plans;
            uint32_t lenTok = 0;
            uint32_t lenBytes = 0;
            uint64_t repTrack = 0;
            size_t repStart = 0;

            if (!buildApplyPlansForCandidate(
                    cand, song.tracks(), segments, globalSeq, prefixBytes, prefixSep, effective,
                    plans, lenTok, lenBytes, repTrack, repStart)) {
                continue;
            }

            applyPlansCreateSubroutineAndRewriteTracks(
                song, plans, lenTok, repTrack, repStart, effective.allowSingleIterationCalls, nextId);
            applied = true;
            break;
        }

        if (!applied) {
            break;
        }
    }
}


}  // namespace ntrak::nspc
