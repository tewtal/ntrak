#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ntrak::nspc {

struct NspcReservedRegion {
    std::string name;
    uint16_t from;  // inclusive
    uint16_t to;    // exclusive
};

enum class NspcEngineHookOperation : uint8_t {
    Execute,
    Read,
    Write,
};

struct NspcEngineHookTrigger {
    NspcEngineHookOperation operation = NspcEngineHookOperation::Write;
    uint16_t address = 0;
    std::optional<uint8_t> value = std::nullopt;
    bool includeDummy = false;
    uint16_t count = 1;
};

struct NspcEnginePlaybackHooks {
    std::optional<NspcEngineHookTrigger> tickTrigger;
    std::optional<NspcEngineHookTrigger> patternTrigger;
};

struct NspcCommandMap {
    // Raw engine byte ranges for note/percussion and raw IDs for tie/rest.
    uint8_t noteStart = 0x80;
    uint8_t noteEnd = 0xC7;
    uint8_t tie = 0xC8;
    uint8_t restStart = 0xC9;
    uint8_t restEnd = 0xC9;
    uint8_t restWrite = 0xC9;
    uint8_t percussionStart = 0xCA;
    uint8_t percussionEnd = 0xDF;
    uint8_t vcmdStart = 0xE0;

    // Raw engine VCMD byte -> internal/common VCMD byte.
    std::unordered_map<uint8_t, uint8_t> readVcmdMap;
    // Internal/common VCMD byte -> raw engine VCMD byte.
    std::unordered_map<uint8_t, uint8_t> writeVcmdMap;
    // If true, reading an unmapped raw VCMD byte is treated as unknown/invalid.
    bool strictReadVcmdMap = false;
    // If true, compiling an unmapped common VCMD for this engine is a hard error.
    bool strictWriteVcmdMap = false;
};

struct NspcEnginePatchWrite {
    std::string name;
    uint16_t address = 0;
    std::vector<uint8_t> bytes;
};

struct NspcEngineExtensionVcmd {
    uint8_t id = 0;
    std::string name;
    std::string description;
    uint8_t paramCount = 0;
};

struct NspcEngineExtension {
    std::string name;
    std::string description;
    bool enabledByDefault = true;
    bool enabled = true;
    std::vector<NspcEnginePatchWrite> patches;
    std::vector<NspcEngineExtensionVcmd> vcmds;
};

struct NspcEngineConfig {
    std::string id;
    std::string engineVersion;
    std::string name;
    uint16_t entryPoint = 0;
    std::optional<uint16_t> sampleHeaderPtr = std::nullopt;
    std::optional<uint16_t> defaultDspTablePtr = std::nullopt;
    std::optional<uint16_t> echoBufferPtr = std::nullopt;
    std::optional<uint16_t> instrumentHeaderPtrLo = std::nullopt;
    std::optional<uint16_t> instrumentHeaderPtrHi = std::nullopt;
    std::optional<uint16_t> percussionHeaderPtrLo = std::nullopt;
    std::optional<uint16_t> percussionHeaderPtrHi = std::nullopt;
    std::optional<uint16_t> songIndexPtr = std::nullopt;
    uint16_t sampleHeaders = 0;
    uint16_t instrumentHeaders = 0;
    uint16_t percussionHeaders = 0;
    uint16_t songIndexPointers = 0;
    uint8_t songTriggerPort = 0;
    uint8_t songTriggerOffset = 1;
    uint8_t instrumentEntryBytes = 6;
    uint16_t echoBuffer = 0;
    uint16_t echoBufferLen = 0;
    std::vector<uint8_t> engineBytes;
    std::vector<NspcReservedRegion> reserved;
    std::optional<NspcEnginePlaybackHooks> playbackHooks;
    std::optional<NspcCommandMap> commandMap;
    uint8_t extensionVcmdPrefix = 0xFF;
    std::vector<NspcEngineExtension> extensions;

    // Optional defaults used to classify imported SPC content.
    // If any "hasDefault..." flag is false, that content category defaults to EngineProvided.
    std::vector<int> defaultEngineProvidedSongIds;
    std::vector<int> defaultEngineProvidedInstrumentIds;
    std::vector<int> defaultEngineProvidedSampleIds;
    bool hasDefaultEngineProvidedSongs = false;
    bool hasDefaultEngineProvidedInstruments = false;
    bool hasDefaultEngineProvidedSamples = false;
};

const NspcEngineExtension* findEngineExtension(const NspcEngineConfig& config, std::string_view name);
const NspcEngineExtensionVcmd* findEngineExtensionVcmd(const NspcEngineConfig& config, uint8_t id,
                                                        bool enabledOnly = true);
std::optional<uint8_t> extensionVcmdParamByteCount(const NspcEngineConfig& config, uint8_t id,
                                                   bool enabledOnly = true);

std::optional<std::vector<NspcEngineConfig>> loadEngineConfigs();
NspcEngineConfig resolveEngineConfigPointers(const NspcEngineConfig& config, std::span<const std::uint8_t> aram);

}  // namespace ntrak::nspc
