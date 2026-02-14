#pragma once

#include "ntrak/nspc/NspcCompile.hpp"
#include "ntrak/nspc/NspcProject.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ntrak::nspc {

/// Build an SPC file from a project that auto-plays the specified song.
/// This function:
/// - Compiles the song and applies it to the base SPC image
/// - Initializes the emulator state to trigger the song on load
/// - Returns a complete SPC file ready for playback
///
/// @param project The project containing the song to export
/// @param baseSpcImage The base SPC image to patch (must be valid SPC file)
/// @param songIndex The index of the song to auto-play
/// @param triggerPortOverride Optional override for song trigger port (0-3)
/// @return The complete SPC file data, or an error message
std::expected<std::vector<uint8_t>, std::string> buildAutoPlaySpc(
    NspcProject& project,
    std::span<const uint8_t> baseSpcImage,
    int songIndex,
    std::optional<uint8_t> triggerPortOverride = std::nullopt,
    NspcBuildOptions buildOptions = {});

}  // namespace ntrak::nspc
