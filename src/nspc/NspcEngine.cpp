#include "ntrak/nspc/NspcEngine.hpp"

#include "ntrak/common/Paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <string_view>
using json = nlohmann::json;

namespace ntrak::nspc {
namespace {

std::optional<uint8_t> readAramByte(std::span<const std::uint8_t> aram, uint16_t address) {
    const size_t index = static_cast<size_t>(address);
    if (index >= aram.size()) {
        return std::nullopt;
    }
    return aram[index];
}

std::optional<uint16_t> readAramWord(std::span<const std::uint8_t> aram, uint16_t address) {
    const size_t index = static_cast<size_t>(address);
    if (index + 1 >= aram.size()) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(aram[index] | (static_cast<uint16_t>(aram[index + 1]) << 8));
}

constexpr uint8_t kDspDirReg = 0x5D;
constexpr uint8_t kDspEchoStartReg = 0x6D;
constexpr uint32_t kDefaultDspTableEntryCount = 12;

std::optional<uint8_t> readDefaultDspValue(std::span<const std::uint8_t> aram, uint16_t defaultTablePtrAddress,
                                           uint8_t dspReg) {
    const auto tableAddress = readAramWord(aram, defaultTablePtrAddress);
    if (!tableAddress.has_value()) {
        return std::nullopt;
    }

    const uint32_t valuesBase = *tableAddress;
    const uint32_t regsBase = valuesBase + kDefaultDspTableEntryCount;
    for (uint32_t i = 0; i < kDefaultDspTableEntryCount; ++i) {
        const uint32_t valueAddr = valuesBase + i;
        const uint32_t regAddr = regsBase + i;
        if (valueAddr >= aram.size() || regAddr >= aram.size()) {
            return std::nullopt;
        }
        if (aram[regAddr] == dspReg) {
            return aram[valueAddr];
        }
    }

    return std::nullopt;
}

std::optional<json> loadJsonFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    json parsed;
    try {
        in >> parsed;
        return parsed;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void mergeJsonObject(json& base, const json& overrides) {
    if (!base.is_object() || !overrides.is_object()) {
        return;
    }

    for (const auto& [key, overrideValue] : overrides.items()) {
        if (overrideValue.is_null()) {
            base.erase(key);
            continue;
        }

        auto baseIt = base.find(key);
        if (baseIt != base.end() && baseIt->is_object() && overrideValue.is_object()) {
            mergeJsonObject(*baseIt, overrideValue);
            continue;
        }

        base[key] = overrideValue;
    }
}

std::optional<std::string> readNonEmptyJsonString(const json& value, const char* key) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto it = value.find(key);
    if (it == value.end() || !it->is_string()) {
        return std::nullopt;
    }
    auto parsed = it->get<std::string>();
    if (parsed.empty()) {
        return std::nullopt;
    }
    return parsed;
}

std::vector<std::string> engineLookupKeys(const json& value) {
    std::vector<std::string> keys;
    if (const auto id = readNonEmptyJsonString(value, "id"); id.has_value()) {
        keys.push_back("id:" + *id);
    }
    if (const auto name = readNonEmptyJsonString(value, "name"); name.has_value()) {
        keys.push_back("name:" + *name);
    }
    return keys;
}

void registerEngineLookupKeys(const json& value, size_t index, std::unordered_map<std::string, size_t>& lookup) {
    for (const auto& key : engineLookupKeys(value)) {
        lookup[key] = index;
    }
}

std::optional<size_t> findEngineOverrideTarget(const json& overrideEntry,
                                               const std::unordered_map<std::string, size_t>& lookup) {
    for (const auto& key : engineLookupKeys(overrideEntry)) {
        const auto it = lookup.find(key);
        if (it != lookup.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

void applyEngineOverrides(json& baseConfigs, const json& overrideConfigs) {
    if (!baseConfigs.is_array() || !overrideConfigs.is_array()) {
        return;
    }

    std::unordered_map<std::string, size_t> engineIndexLookup;
    for (size_t i = 0; i < baseConfigs.size(); ++i) {
        registerEngineLookupKeys(baseConfigs[i], i, engineIndexLookup);
    }

    for (const auto& overrideEntry : overrideConfigs) {
        if (!overrideEntry.is_object()) {
            continue;
        }
        if (engineLookupKeys(overrideEntry).empty()) {
            continue;
        }

        const auto targetIndex = findEngineOverrideTarget(overrideEntry, engineIndexLookup);
        if (!targetIndex.has_value()) {
            baseConfigs.push_back(overrideEntry);
            registerEngineLookupKeys(baseConfigs.back(), baseConfigs.size() - 1, engineIndexLookup);
            continue;
        }

        auto& baseEntry = baseConfigs[*targetIndex];
        if (!baseEntry.is_object()) {
            baseEntry = overrideEntry;
        } else {
            mergeJsonObject(baseEntry, overrideEntry);
        }
        registerEngineLookupKeys(baseEntry, *targetIndex, engineIndexLookup);
    }
}

}  // namespace

std::vector<uint8_t> parseHexBytes(std::string_view hexStr) {
    std::vector<uint8_t> bytes;
    std::string packed;
    packed.reserve(hexStr.size());
    for (const unsigned char ch : hexStr) {
        if (std::isxdigit(ch) == 0) {
            continue;
        }
        packed.push_back(static_cast<char>(std::toupper(ch)));
    }

    if ((packed.size() % 2) != 0) {
        packed.pop_back();
    }

    bytes.reserve(packed.size() / 2);
    for (size_t i = 0; i + 1 < packed.size(); i += 2) {
        const std::string_view pair(packed.data() + static_cast<ptrdiff_t>(i), 2);
        unsigned value = 0;
        const auto [ptr, ec] = std::from_chars(pair.data(), pair.data() + pair.size(), value, 16);
        if (ec != std::errc()) {
            continue;
        }
        bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
    }
    return bytes;
}

uint16_t parseHexValue(std::string_view hexStr) {
    if (hexStr.starts_with("0x") || hexStr.starts_with("0X")) {
        hexStr.remove_prefix(2);
    } else if (hexStr.starts_with("$")) {
        hexStr.remove_prefix(1);
    }

    unsigned value{};
    auto [ptr, ec] = std::from_chars(hexStr.data(), hexStr.data() + hexStr.size(), value, 16);
    if (ec == std::errc()) {
        return static_cast<uint16_t>(value);
    } else {
        return 0;
    }
}

uint16_t parseHexJson(const json& value) {
    if (value.is_string()) {
        return parseHexValue(value.get<std::string>());
    }
    if (value.is_number_unsigned()) {
        return static_cast<uint16_t>(value.get<uint32_t>() & 0xFFFFu);
    }
    if (value.is_number_integer()) {
        const int32_t raw = value.get<int32_t>();
        return static_cast<uint16_t>(std::clamp(raw, 0, 0xFFFF));
    }
    return 0;
}

std::string toLowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::optional<NspcEngineHookOperation> parseHookOperation(std::string_view value) {
    const std::string op = toLowercase(std::string(value));
    if (op == "exec" || op == "execute" || op == "opcode") {
        return NspcEngineHookOperation::Execute;
    }
    if (op == "read" || op == "r") {
        return NspcEngineHookOperation::Read;
    }
    if (op == "write" || op == "w") {
        return NspcEngineHookOperation::Write;
    }
    return std::nullopt;
}

std::optional<NspcEngineHookTrigger> parseHookTrigger(const json& value) {
    if (!value.is_object() || !value.contains("address")) {
        return std::nullopt;
    }

    NspcEngineHookTrigger trigger{};
    trigger.address = parseHexJson(value["address"]);
    if (value.contains("op") && value["op"].is_string()) {
        if (const auto op = parseHookOperation(value["op"].get<std::string>()); op.has_value()) {
            trigger.operation = *op;
        }
    }
    if (value.contains("value")) {
        trigger.value = static_cast<uint8_t>(parseHexJson(value["value"]) & 0xFFu);
    }
    if (value.contains("includeDummy") && value["includeDummy"].is_boolean()) {
        trigger.includeDummy = value["includeDummy"].get<bool>();
    }
    if (value.contains("count")) {
        const uint16_t parsedCount = parseHexJson(value["count"]);
        trigger.count = std::max<uint16_t>(1u, parsedCount);
    }
    return trigger;
}

uint8_t parseByteJson(const json& value) {
    return static_cast<uint8_t>(parseHexJson(value) & 0xFFu);
}

std::vector<uint8_t> parsePatchBytes(const json& value) {
    if (value.is_string()) {
        return parseHexBytes(value.get<std::string>());
    }
    if (!value.is_array()) {
        return {};
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(value.size());
    for (const auto& byteValue : value) {
        bytes.push_back(parseByteJson(byteValue));
    }
    return bytes;
}

std::optional<NspcEnginePatchWrite> parsePatchWrite(const json& value, std::string defaultName) {
    if (!value.is_object() || !value.contains("address") || !value.contains("bytes")) {
        return std::nullopt;
    }

    NspcEnginePatchWrite patch{};
    patch.name = value.value("name", std::move(defaultName));
    patch.address = parseHexJson(value["address"]);
    patch.bytes = parsePatchBytes(value["bytes"]);
    if (patch.bytes.empty()) {
        return std::nullopt;
    }
    return patch;
}

std::optional<NspcEngineExtensionVcmd> parseExtensionVcmd(const json& value) {
    if (!value.is_object() || !value.contains("id")) {
        return std::nullopt;
    }

    NspcEngineExtensionVcmd vcmd{};
    vcmd.id = parseByteJson(value["id"]);
    vcmd.name = value.value("name", "");
    vcmd.description = value.value("description", "");

    uint16_t paramCount = 0;
    if (value.contains("paramCount")) {
        paramCount = parseHexJson(value["paramCount"]);
    } else if (value.contains("parameters") && value["parameters"].is_array()) {
        paramCount = static_cast<uint16_t>(value["parameters"].size());
    }
    vcmd.paramCount = static_cast<uint8_t>(std::clamp<uint16_t>(paramCount, 0u, 4u));
    return vcmd;
}

std::vector<NspcEngineExtension> parseExtensions(const json& value) {
    std::vector<NspcEngineExtension> extensions;
    if (!value.is_array()) {
        return extensions;
    }

    extensions.reserve(value.size());
    for (const auto& entry : value) {
        if (!entry.is_object()) {
            continue;
        }

        NspcEngineExtension extension{};
        extension.name = entry.value("name", "");
        extension.description = entry.value("description", "");
        if (entry.contains("enabledByDefault") && entry["enabledByDefault"].is_boolean()) {
            extension.enabledByDefault = entry["enabledByDefault"].get<bool>();
        } else if (entry.contains("enabled") && entry["enabled"].is_boolean()) {
            extension.enabledByDefault = entry["enabled"].get<bool>();
        }
        extension.enabled = extension.enabledByDefault;

        if (entry.contains("code")) {
            if (const auto patch = parsePatchWrite(entry["code"], "Code"); patch.has_value()) {
                extension.patches.push_back(*patch);
            }
        }
        if (entry.contains("hooks") && entry["hooks"].is_array()) {
            for (const auto& hook : entry["hooks"]) {
                if (const auto patch = parsePatchWrite(hook, "Hook"); patch.has_value()) {
                    extension.patches.push_back(*patch);
                }
            }
        }
        if (entry.contains("patches") && entry["patches"].is_array()) {
            for (const auto& patchEntry : entry["patches"]) {
                if (const auto patch = parsePatchWrite(patchEntry, "Patch"); patch.has_value()) {
                    extension.patches.push_back(*patch);
                }
            }
        }

        if (entry.contains("vcmds") && entry["vcmds"].is_array()) {
            for (const auto& vcmdEntry : entry["vcmds"]) {
                if (const auto vcmd = parseExtensionVcmd(vcmdEntry); vcmd.has_value()) {
                    extension.vcmds.push_back(*vcmd);
                }
            }
        }

        if (!extension.name.empty() && (!extension.patches.empty() || !extension.vcmds.empty())) {
            extensions.push_back(std::move(extension));
        }
    }

    return extensions;
}

std::unordered_map<uint8_t, uint8_t> parseByteMapObject(const json& value) {
    std::unordered_map<uint8_t, uint8_t> map;
    if (!value.is_object()) {
        return map;
    }

    for (const auto& [key, mappedValue] : value.items()) {
        const uint8_t source = static_cast<uint8_t>(parseHexValue(key) & 0xFFu);
        const uint8_t target = static_cast<uint8_t>(parseHexJson(mappedValue) & 0xFFu);
        map[source] = target;
    }

    return map;
}

std::vector<int> parseEngineProvidedIds(const json& value) {
    std::vector<int> ids;
    if (!value.is_array()) {
        return ids;
    }

    auto appendId = [&](int id) {
        if (id < 0 || id > 0xFF) {
            return;
        }
        ids.push_back(id);
    };

    auto appendRange = [&](int from, int to) {
        if (to < from) {
            std::swap(from, to);
        }
        from = std::clamp(from, 0, 0xFF);
        to = std::clamp(to, 0, 0xFF);
        for (int id = from; id <= to; ++id) {
            ids.push_back(id);
        }
    };

    for (const auto& entry : value) {
        if (entry.is_object()) {
            if (entry.contains("id")) {
                appendId(static_cast<int>(parseHexJson(entry["id"])));
                continue;
            }

            if (entry.contains("from") || entry.contains("to")) {
                const int from = entry.contains("from") ? static_cast<int>(parseHexJson(entry["from"])) : 0;
                const int to = entry.contains("to") ? static_cast<int>(parseHexJson(entry["to"])) : from;
                appendRange(from, to);
            }
            continue;
        }

        appendId(static_cast<int>(parseHexJson(entry)));
    }

    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

NspcCommandMap parseCommandMap(const json& value) {
    NspcCommandMap map{};
    if (!value.is_object()) {
        return map;
    }

    const bool hasRestWrite = value.contains("restWrite");
    const bool hasVcmdStart = value.contains("vcmdStart");

    if (value.contains("noteStart")) {
        map.noteStart = parseByteJson(value["noteStart"]);
    }
    if (value.contains("noteEnd")) {
        map.noteEnd = parseByteJson(value["noteEnd"]);
    }
    if (value.contains("tie")) {
        map.tie = parseByteJson(value["tie"]);
    }
    if (value.contains("restStart")) {
        map.restStart = parseByteJson(value["restStart"]);
    }
    if (value.contains("restEnd")) {
        map.restEnd = parseByteJson(value["restEnd"]);
    }
    if (value.contains("restWrite")) {
        map.restWrite = parseByteJson(value["restWrite"]);
    }
    if (value.contains("percussionStart")) {
        map.percussionStart = parseByteJson(value["percussionStart"]);
    }
    if (value.contains("percussionEnd")) {
        map.percussionEnd = parseByteJson(value["percussionEnd"]);
    }
    if (value.contains("vcmdStart")) {
        map.vcmdStart = parseByteJson(value["vcmdStart"]);
    }

    if (!hasRestWrite) {
        map.restWrite = map.restStart;
    }

    if (map.noteEnd < map.noteStart) {
        std::swap(map.noteStart, map.noteEnd);
    }
    if (map.restEnd < map.restStart) {
        std::swap(map.restStart, map.restEnd);
    }
    if (map.percussionEnd < map.percussionStart) {
        std::swap(map.percussionStart, map.percussionEnd);
    }

    if (value.contains("readVcmdMap")) {
        map.readVcmdMap = parseByteMapObject(value["readVcmdMap"]);
    }
    if (value.contains("writeVcmdMap")) {
        map.writeVcmdMap = parseByteMapObject(value["writeVcmdMap"]);
    }
    if (map.writeVcmdMap.empty()) {
        for (const auto& [rawId, commonId] : map.readVcmdMap) {
            if (!map.writeVcmdMap.contains(commonId)) {
                map.writeVcmdMap[commonId] = rawId;
            }
        }
    }
    if (!hasVcmdStart && !map.readVcmdMap.empty()) {
        const auto minId = std::min_element(map.readVcmdMap.begin(), map.readVcmdMap.end(),
                                            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        map.vcmdStart = std::min(map.vcmdStart, minId->first);
    }

    if (value.contains("strictWriteVcmdMap") && value["strictWriteVcmdMap"].is_boolean()) {
        map.strictWriteVcmdMap = value["strictWriteVcmdMap"].get<bool>();
    }
    if (value.contains("strictReadVcmdMap") && value["strictReadVcmdMap"].is_boolean()) {
        map.strictReadVcmdMap = value["strictReadVcmdMap"].get<bool>();
    }

    return map;
}

void parseEngineProvidedDefaults(const json& item, NspcEngineConfig& config) {
    auto parseEngineProvidedObject = [&](const json& obj) {
        if (!obj.is_object()) {
            return;
        }
        if (obj.contains("songs")) {
            config.defaultEngineProvidedSongIds = parseEngineProvidedIds(obj["songs"]);
            config.hasDefaultEngineProvidedSongs = true;
        }
        if (obj.contains("instruments")) {
            config.defaultEngineProvidedInstrumentIds = parseEngineProvidedIds(obj["instruments"]);
            config.hasDefaultEngineProvidedInstruments = true;
        }
        if (obj.contains("samples")) {
            config.defaultEngineProvidedSampleIds = parseEngineProvidedIds(obj["samples"]);
            config.hasDefaultEngineProvidedSamples = true;
        }
    };

    if (item.contains("engineProvided")) {
        parseEngineProvidedObject(item["engineProvided"]);
    }
    if (item.contains("engineProvidedSongs")) {
        config.defaultEngineProvidedSongIds = parseEngineProvidedIds(item["engineProvidedSongs"]);
        config.hasDefaultEngineProvidedSongs = true;
    }
    if (item.contains("engineProvidedInstruments")) {
        config.defaultEngineProvidedInstrumentIds = parseEngineProvidedIds(item["engineProvidedInstruments"]);
        config.hasDefaultEngineProvidedInstruments = true;
    }
    if (item.contains("engineProvidedSamples")) {
        config.defaultEngineProvidedSampleIds = parseEngineProvidedIds(item["engineProvidedSamples"]);
        config.hasDefaultEngineProvidedSamples = true;
    }
}

NspcEngineConfig parseEngineConfigEntry(const json& item) {
    NspcEngineConfig config;
    config.id = item.value("id", "");
    config.name = item.value("name", "");
    config.entryPoint = item.contains("entryPoint") ? parseHexJson(item["entryPoint"]) : 0;

    if (item.contains("sampleHeaderPtr")) {
        config.sampleHeaderPtr = parseHexJson(item["sampleHeaderPtr"]);
    }
    if (item.contains("defaultDspTablePtr")) {
        config.defaultDspTablePtr = parseHexJson(item["defaultDspTablePtr"]);
    }
    if (item.contains("echoBufferPtr")) {
        config.echoBufferPtr = parseHexJson(item["echoBufferPtr"]);
    }
    if (item.contains("instrumentHeaderPtrLo")) {
        config.instrumentHeaderPtrLo = parseHexJson(item["instrumentHeaderPtrLo"]);
    }
    if (item.contains("instrumentHeaderPtrHi")) {
        config.instrumentHeaderPtrHi = parseHexJson(item["instrumentHeaderPtrHi"]);
    }
    if (item.contains("songIndexPtr")) {
        config.songIndexPtr = parseHexJson(item["songIndexPtr"]);
    }

    config.sampleHeaders = item.contains("sampleHeaders") ? parseHexJson(item["sampleHeaders"]) : 0;
    config.instrumentHeaders = item.contains("instrumentHeaders") ? parseHexJson(item["instrumentHeaders"]) : 0;
    config.songIndexPointers = item.contains("songIndexPointers") ? parseHexJson(item["songIndexPointers"]) : 0;
    if (item.contains("songTriggerPort")) {
        config.songTriggerPort = static_cast<uint8_t>(parseHexJson(item["songTriggerPort"]) & 0x03u);
    }
    if (item.contains("songTriggerOffset")) {
        config.songTriggerOffset = static_cast<uint8_t>(parseHexJson(item["songTriggerOffset"]) & 0xFFu);
    }
    if (item.contains("instrumentEntryBytes")) {
        const uint16_t parsedSize = parseHexJson(item["instrumentEntryBytes"]);
        config.instrumentEntryBytes = static_cast<uint8_t>(std::clamp<uint16_t>(parsedSize, 5u, 6u));
    }
    config.echoBuffer = item.contains("echoBuffer") ? parseHexJson(item["echoBuffer"]) : 0;
    config.echoBufferLen = item.contains("echoBufferLen") ? parseHexJson(item["echoBufferLen"]) : 0;
    if (item.contains("engineBytes") && item["engineBytes"].is_string()) {
        config.engineBytes = parseHexBytes(item["engineBytes"].get<std::string>());
    }
    if (item.contains("reserved") && item["reserved"].is_array()) {
        for (const auto& region : item["reserved"]) {
            if (!region.is_object()) {
                continue;
            }

            const uint16_t from = region.contains("from") ? parseHexJson(region["from"]) : 0;
            const uint16_t to = region.contains("to") ? parseHexJson(region["to"]) : from;
            if (to <= from) {
                continue;
            }

            config.reserved.push_back(NspcReservedRegion{
                .name = region.value("name", "Reserved"),
                .from = from,
                .to = to,
            });
        }
    }
    if (item.contains("playbackHooks") && item["playbackHooks"].is_object()) {
        const auto& hooksValue = item["playbackHooks"];
        NspcEnginePlaybackHooks hooks{};

        if (hooksValue.contains("tickTrigger")) {
            hooks.tickTrigger = parseHookTrigger(hooksValue["tickTrigger"]);
        }
        if (hooksValue.contains("patternTrigger")) {
            hooks.patternTrigger = parseHookTrigger(hooksValue["patternTrigger"]);
        }

        if (hooks.tickTrigger.has_value() || hooks.patternTrigger.has_value()) {
            config.playbackHooks = hooks;
        }
    }
    if (item.contains("cmdMap")) {
        config.commandMap = parseCommandMap(item["cmdMap"]);
    }
    if (item.contains("extensionVcmdPrefix")) {
        config.extensionVcmdPrefix = parseByteJson(item["extensionVcmdPrefix"]);
    }
    if (item.contains("extensions")) {
        config.extensions = parseExtensions(item["extensions"]);
    }

    parseEngineProvidedDefaults(item, config);
    return config;
}

std::optional<std::vector<NspcEngineConfig>> loadEngineConfigs() {
    const auto bundledPath = ntrak::common::bundledEngineConfigPath();
    auto mergedConfig = loadJsonFile(bundledPath);
    if (!mergedConfig.has_value() || !mergedConfig->is_array()) {
        return std::nullopt;
    }

    const auto overridePath = ntrak::common::userEngineOverridePath();
    if (!overridePath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(overridePath, ec) && !ec) {
            auto overrideConfig = loadJsonFile(overridePath);
            if (!overrideConfig.has_value() || !overrideConfig->is_array()) {
                return std::nullopt;
            }
            applyEngineOverrides(*mergedConfig, *overrideConfig);
        }
    }

    try {
        std::vector<NspcEngineConfig> configs;
        configs.reserve(mergedConfig->size());
        for (const auto& item : *mergedConfig) {
            if (!item.is_object()) {
                continue;
            }
            configs.push_back(parseEngineConfigEntry(item));
        }
        return configs;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

NspcEngineConfig resolveEngineConfigPointers(const NspcEngineConfig& config, std::span<const std::uint8_t> aram) {
    NspcEngineConfig resolved = config;

    if (config.sampleHeaderPtr.has_value()) {
        if (const auto sampleHeaders = readAramByte(aram, *config.sampleHeaderPtr); sampleHeaders.has_value()) {
            resolved.sampleHeaders = (*sampleHeaders << 8);
        }
    }
    if (config.defaultDspTablePtr.has_value()) {
        if (const auto sampleHeaders = readDefaultDspValue(aram, *config.defaultDspTablePtr, kDspDirReg);
            sampleHeaders.has_value()) {
            resolved.sampleHeaders = static_cast<uint16_t>(*sampleHeaders << 8u);
        }

        if (const auto echoStart = readDefaultDspValue(aram, *config.defaultDspTablePtr, kDspEchoStartReg);
            echoStart.has_value()) {
            resolved.echoBuffer = *echoStart << 8;
        }
    }
    if (config.echoBufferPtr.has_value()) {
        if (const auto echoStart = readAramByte(aram, *config.echoBufferPtr); echoStart.has_value()) {
            resolved.echoBuffer = *echoStart << 8;
        }
    }

    const auto instrumentLo = config.instrumentHeaderPtrLo.has_value() ? readAramByte(aram, *config.instrumentHeaderPtrLo)
                                                                       : std::nullopt;
    const auto instrumentHi = config.instrumentHeaderPtrHi.has_value() ? readAramByte(aram, *config.instrumentHeaderPtrHi)
                                                                       : std::nullopt;
    if (instrumentLo.has_value() && instrumentHi.has_value()) {
        resolved.instrumentHeaders =
            static_cast<uint16_t>(*instrumentLo | (static_cast<uint16_t>(*instrumentHi) << 8));
    }

    if (config.songIndexPtr.has_value()) {
        if (const auto songIndex = readAramWord(aram, *config.songIndexPtr); songIndex.has_value()) {
            resolved.songIndexPointers = *songIndex + 1; // The address in the engine is one off from the actual song index data
        }
    }

    return resolved;
}

const NspcEngineExtension* findEngineExtension(const NspcEngineConfig& config, std::string_view name) {
    const auto it =
        std::find_if(config.extensions.begin(), config.extensions.end(),
                     [name](const NspcEngineExtension& extension) { return extension.name == name; });
    if (it == config.extensions.end()) {
        return nullptr;
    }
    return &(*it);
}

const NspcEngineExtensionVcmd* findEngineExtensionVcmd(const NspcEngineConfig& config, uint8_t id, bool enabledOnly) {
    for (const auto& extension : config.extensions) {
        if (enabledOnly && !extension.enabled) {
            continue;
        }
        const auto it = std::find_if(extension.vcmds.begin(), extension.vcmds.end(),
                                     [id](const NspcEngineExtensionVcmd& vcmd) { return vcmd.id == id; });
        if (it != extension.vcmds.end()) {
            return &(*it);
        }
    }
    return nullptr;
}

std::optional<uint8_t> extensionVcmdParamByteCount(const NspcEngineConfig& config, uint8_t id, bool enabledOnly) {
    if (const auto* vcmd = findEngineExtensionVcmd(config, id, enabledOnly); vcmd != nullptr) {
        return vcmd->paramCount;
    }
    return std::nullopt;
}
}  // namespace ntrak::nspc
