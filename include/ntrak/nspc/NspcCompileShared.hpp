#pragma once

#include "ntrak/nspc/NspcCompile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ntrak::nspc::compile_detail {

constexpr std::size_t kSpcHeaderSize = 0x100;
constexpr std::size_t kAramSize = 0x10000;

struct AddressRange {
    uint32_t from = 0;  // inclusive
    uint32_t to = 0;    // exclusive
};

enum class AllocObjectKind : uint8_t {
    Sequence,
    Pattern,
    Track,
    Subroutine,
};

struct AllocRequest {
    AllocObjectKind kind = AllocObjectKind::Sequence;
    int id = -1;
    std::optional<uint16_t> preferredAddr;
    uint32_t size = 0;
    std::string label;
};

void appendU8(std::vector<uint8_t>& out, uint8_t value);
void appendU16(std::vector<uint8_t>& out, uint16_t value);
uint32_t sequenceOpSize(const NspcSequenceOp& op);
bool isRelocatableSongRegion(const NspcAramRegion& region, int songId);
void addClampedRange(std::vector<AddressRange>& ranges, uint32_t from, uint32_t to);
void normalizeRanges(std::vector<AddressRange>& ranges);
std::vector<AddressRange> invertRanges(const std::vector<AddressRange>& blockedRanges);
uint32_t totalRangeBytes(const std::vector<AddressRange>& ranges);
std::optional<uint16_t> allocateFromFreeRanges(std::vector<AddressRange>& freeRanges, uint32_t size,
                                               std::optional<uint16_t> preferredAddr);
std::optional<uint16_t> readSongSequencePointer(emulation::AramView aram, const NspcEngineConfig& engine,
                                                size_t songIndex);

std::expected<std::vector<uint8_t>, std::string> encodeEventStream(
    const std::vector<NspcEventEntry>& events, const std::unordered_map<int, uint16_t>& subroutineAddrById,
    std::vector<std::string>& warnings, const NspcEngineConfig& engine);

std::vector<uint8_t> buildSequencePointerMask(const std::vector<NspcSequenceOp>& sequence, size_t encodedSize);
std::vector<uint8_t> buildPatternPointerMask(size_t size);
std::vector<uint8_t> buildStreamPointerMask(const std::vector<NspcEventEntry>& events, size_t encodedSize);
std::expected<std::vector<uint8_t>, std::string> readAramBytes(emulation::AramView aram, uint16_t address, size_t size,
                                                               std::string_view label);
void compareBinaryObject(std::string_view label, std::span<const uint8_t> original, std::span<const uint8_t> rebuilt,
                         std::span<const uint8_t> pointerMask, NspcRoundTripReport& report);
std::vector<NspcUploadChunk> buildEnabledEngineExtensionPatchChunks(const NspcEngineConfig& engine);

void sortUploadChunksByAddress(std::vector<NspcUploadChunk>& chunks, bool stableSort);
std::expected<void, std::string> validateUploadChunkBoundsAndOverlap(const std::vector<NspcUploadChunk>& chunks,
                                                                     bool detailedOverlapMessage);

}  // namespace ntrak::nspc::compile_detail
