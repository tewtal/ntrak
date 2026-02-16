#pragma once
#include "ntrak/emulation/SpcDsp.hpp"
#include "ntrak/nspc/NspcData.hpp"
#include "ntrak/nspc/NspcEngine.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ntrak::nspc {

enum class NspcAramRegionKind : uint8_t {
    Free,
    Reserved,
    SongIndexTable,
    InstrumentTable,
    SampleDirectory,
    SampleData,
    SequenceData,
    PatternTable,
    TrackData,
    SubroutineData,
};

struct NspcAramRegion {
    NspcAramRegionKind kind = NspcAramRegionKind::Free;
    uint16_t from = 0;  // inclusive
    uint16_t to = 0;    // exclusive
    int songId = -1;
    int objectId = -1;
    std::string label;
};

struct NspcAramUsage {
    static constexpr uint32_t kTotalAramBytes = 0x10000;

    uint32_t totalBytes = kTotalAramBytes;
    uint32_t usedBytes = 0;
    uint32_t freeBytes = 0;

    uint32_t reservedBytes = 0;
    uint32_t songIndexBytes = 0;
    uint32_t instrumentBytes = 0;
    uint32_t sampleDirectoryBytes = 0;
    uint32_t sampleDataBytes = 0;
    uint32_t sequenceBytes = 0;
    uint32_t patternTableBytes = 0;
    uint32_t trackBytes = 0;
    uint32_t subroutineBytes = 0;

    std::vector<NspcAramRegion> regions;
};

struct NspcSongAddressLayout {
    uint16_t sequenceAddr = 0;
    std::unordered_map<int, uint16_t> patternAddrById;
    std::unordered_map<int, uint16_t> trackAddrById;
    std::unordered_map<int, uint16_t> subroutineAddrById;
    std::unordered_map<int, uint32_t> trackSizeById;
    std::unordered_map<int, uint32_t> subroutineSizeById;
};

class NspcProject {
public:
    NspcProject(NspcEngineConfig config, std::array<std::uint8_t, 0x10000> aramData);
    ~NspcProject() = default;

    NspcEngineConfig& engineConfig() { return engineConfig_; }

    const NspcEngineConfig& engineConfig() const { return engineConfig_; }

    emulation::AramView aram() { return emulation::AramView(aram_.data(), aram_.size()); }

    emulation::AramView aram() const { return emulation::AramView(const_cast<uint8_t*>(aram_.data()), aram_.size()); }

    const std::vector<NspcSong>& songs() const { return songs_; }

    std::vector<NspcSong>& songs() { return songs_; }

    const std::vector<NspcInstrument>& instruments() const { return instruments_; }

    std::vector<NspcInstrument>& instruments() { return instruments_; }

    const std::vector<BrrSample>& samples() const { return samples_; }

    std::vector<BrrSample>& samples() { return samples_; }

    const NspcAramUsage& aramUsage() const { return aramUsage_; }

    std::optional<size_t> addEmptySong();
    std::optional<size_t> duplicateSong(size_t songIndex);
    bool removeSong(size_t songIndex);
    bool setSongContentOrigin(size_t songIndex, NspcContentOrigin origin);
    bool setInstrumentContentOrigin(int instrumentId, NspcContentOrigin origin);
    bool setSampleContentOrigin(int sampleId, NspcContentOrigin origin);
    bool setEngineExtensionEnabled(std::string_view extensionName, bool enabled);
    bool isEngineExtensionEnabled(std::string_view extensionName) const;
    std::vector<std::string> enabledEngineExtensionNames() const;

    const NspcSongAddressLayout* songAddressLayout(int songId) const;
    void setSongAddressLayout(int songId, NspcSongAddressLayout layout);
    void clearSongAddressLayout(int songId);
    void refreshAramUsage();

    const std::vector<std::uint8_t>& sourceSpcData() const { return sourceSpcData_; }

    std::vector<std::uint8_t>& sourceSpcData() { return sourceSpcData_; }

    void setSourceSpcData(std::vector<std::uint8_t> data) { sourceSpcData_ = std::move(data); }

    const std::optional<std::filesystem::path>& sourceSpcPath() const { return sourceSpcPath_; }

    void setSourceSpcPath(std::optional<std::filesystem::path> path) { sourceSpcPath_ = std::move(path); }

    /// Sync entire ARAM back into the SPC image.
    void syncAramToSpcData();
    /// Sync a specific ARAM address range into the SPC image.
    void syncAramRangeToSpcData(uint16_t addr, size_t size);

private:
    void parseInstruments();
    void parseExtendedInstruments(emulation::AramView aramView, uint8_t entrySize);
    void parseSamples();
    void parseSongs();
    void rebuildAramUsage();

    NspcEngineConfig engineConfig_;
    std::array<std::uint8_t, 0x10000> aram_;

    std::vector<NspcSong> songs_;
    std::vector<NspcInstrument> instruments_;
    std::vector<BrrSample> samples_;
    NspcAramUsage aramUsage_;
    std::unordered_map<int, NspcSongAddressLayout> songAddressLayouts_;
    std::vector<std::uint8_t> sourceSpcData_;
    std::optional<std::filesystem::path> sourceSpcPath_;
};

}  // namespace ntrak::nspc
