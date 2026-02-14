#pragma once

#include "ntrak/nspc/NspcData.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

namespace ntrak::ui::detail {

struct FxCategoryStyle {
    ImU32 bg;
    ImU32 border;
    ImU32 text;
};

struct RawVcmdBytes {
    uint8_t id = 0;
    std::array<uint8_t, 4> params{};
    uint8_t paramCount = 0;
};

std::string noteToString(int note);
std::string hex2(int value);
std::string vcmdChipText(const nspc::Vcmd& cmd);
bool isTieMarker(std::string_view noteText);
bool isRestMarker(std::string_view noteText);
bool canShowInstVol(std::string_view noteText);
ImU32 subroutineColorU32(int subroutineId);
uint8_t vcmdCategory(const nspc::Vcmd& cmd);
std::string vcmdTooltipText(const nspc::Vcmd& cmd);
FxCategoryStyle fxCategoryStyle(uint8_t category);
int parseHexValue(std::string_view text);
const char* itemLabel(int item);
bool isEditableFxId(uint8_t id);
std::optional<RawVcmdBytes> rawVcmdBytes(const nspc::Vcmd& cmd);
std::string vcmdInlineText(uint8_t id, const std::array<uint8_t, 4>& params, uint8_t paramCount);
std::string vcmdPackedHex(uint8_t id, const std::array<uint8_t, 4>& params, uint8_t paramCount);
std::string sanitizeHexInput(std::string_view text);
std::optional<nspc::Vcmd> parseTypedFxCommand(std::string_view text);

}  // namespace ntrak::ui::detail
