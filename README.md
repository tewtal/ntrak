# ntrak

**ntrak** is a cross-platform tracker for writing Super Nintendo Entertainment System (SNES) music in the **N-SPC format family**. It provides pattern editing, sequence editing, sample/instrument tools, and accurate SPC700/S-DSP playback through ares-apu.

## AI Disclaimer
A significant portion of the C++ code for this project has been written with the use of AI Coding Agent tools to speed up development and make it possible to create this tool in a reasonable time frame. So if the code looks strange in places this is likely the cause even though I've reviewed it and attempted to clean things up as good as possible. When it comes to the background research, debugging and reverse engineering though that's all good old manual labour :)

## Overview

ntrak is built for musicians, game developers, and chiptune creators who want SNES-accurate results. It supports multiple game audio engines (Super Metroid, The Legend of Zelda: A Link to the Past, Super Mario World 2: Yoshi's Island, and Super Mario World) while keeping the editing workflow in one interface.

It is designed for **N-SPC and N-SPC-derived engines** used in many Nintendo titles and some third-party games. It is not a generic editor for every SNES music engine format.

## Key Features

### Composition & Editing
- **8-Channel Pattern Editor**: Full tracker-style interface with note input, instruments, volumes, and effects
- **Effects Library**: 32+ effects including vibrato, tremolo, echo, panning, volume fades, and pitch envelopes
- **Guided FX Editor**: Edit effects by name with labeled parameter controls instead of raw hex-only input
- **Sequence Editor**: Build complete songs with pattern arrangement, loops, and flow control
- **Multi-Song Projects**: Manage multiple songs in one project with shared instrument and sample libraries
- **IT Module Import Workbench**: Import `.it` modules directly into a selected song slot with previewed ARAM estimates, resampling controls, and asset replacement options

### Audio & Sound Design
- **Sample Management**: Import WAV files with automatic BRR encoding (SNES compression format)
- **Instrument Design**: Create instruments with ADSR envelopes, gain control, and pitch multipliers
- **Real-time Preview**: Keyboard-based instrument and sample preview
- **BRR Codec**: In-app encoding and decoding of SNES audio format

### Playback & Emulation
- **Accurate SPC700/S-DSP Emulation**: Powered by ares-apu
- **Real-time Playback**: Hear your music exactly as it would sound on SNES hardware
- **Cubic Resampling**: Smooth audio output during playback
- **SPC File Support**: Import and play existing SPC files

### User Interface
- **Dockable Panels**: Flexible workspace with movable, resizable panels
- **ARAM Usage Visualization**: Color-coded memory usage display showing how your music fits in 64KB
- **Built-in Quick Guide**: Effects reference and keyboard shortcuts in-app
- **Real-time Feedback**: Visual playback tracking and status information

### Optimization & Building
- **Subroutine Optimization**: Automatically detect repeated patterns and convert to subroutines to save memory
- **Engine-Specific Compilation**: Target different SNES game engines with appropriate command sets
- **Build Presets**: Relaxed, Balanced, and Aggressive optimization modes
- **Memory Management**: Tools to help you stay within SNES's 64KB audio RAM limit
- **Engine Extensions**: Toggle configured extension patches (such as Legato/Arpeggio/KOFF variants) per project
- **Song Porting Tools**: Port songs between source/target SPC engines with instrument/sample mapping controls

## Documentation

- **[User Guide](USER_GUIDE.md)**: Start here for setup, first-song walkthrough, and keyboard reference
- **[Style Guide](STYLE_GUIDE.md)**: Developer coding standards and conventions
- **[Third-Party Licenses](THIRD_PARTY_LICENSES.md)**: Attribution for dependencies

## Quick Start

### For Users

1. **Build ntrak** (see [Build Instructions](#build-instructions) below)
2. **Launch the application**: `./build/bin/ntrak`
3. **Load Working Data**: Use `File -> Open Project...`, `File -> Import SPC...`, or `File -> Import NSPC...`
4. **Read the User Guide**: Open [USER_GUIDE.md](USER_GUIDE.md) and follow "Creating Your First Song"
5. **Use In-App Help**: Open the Quick Guide panel (Overview, Effects, and Shortcuts tabs)

### For Developers

1. Clone the repository
2. Review the [STYLE_GUIDE.md](STYLE_GUIDE.md) for coding standards
3. Build with CMake (see below)
4. All dependencies are fetched automatically via CMake `FetchContent`

## Build Instructions

### Requirements

- **CMake 3.20 or higher**
- **C++23-compatible compiler**:
  - GCC 13+
  - Clang 16+
  - MSVC 2022+
- **OpenGL development libraries**
- **Platform-specific**:
  - **Linux**: GTK3 development libraries (for native file dialogs)
    ```bash
    # Debian/Ubuntu
    sudo apt install libgtk-3-dev

    # Fedora/RHEL
    sudo dnf install gtk3-devel

    # Arch Linux
    sudo pacman -S gtk3
    ```
  - **Windows**: No additional dependencies
  - **macOS**: No additional dependencies (uses native file dialogs)

### Building

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build

# Optional: Build with specific configuration (Debug/Release)
cmake --build build --config Release
```

### Running

```bash
# Linux/macOS
./build/bin/ntrak

# Windows
./build/bin/Release/ntrak.exe
```

## Project Structure

```
ntrak/
├── include/ntrak/     # Public headers (mirrored to src/)
│   ├── app/          # Application state and main loop
│   ├── audio/        # Audio playback and processing
│   ├── common/       # Shared utilities
│   ├── emulation/    # SPC700/S-DSP emulation
│   ├── nspc/         # NSPC engine and data structures
│   └── ui/           # User interface panels
├── src/              # Implementation files
├── config/           # Engine configurations (JSON)
├── assets/           # Application assets
├── libs/             # Vendored dependencies (glad)
├── docs/             # Documentation
├── USER_GUIDE.md     # Comprehensive user documentation
├── STYLE_GUIDE.md    # Developer coding standards
└── README.md         # This file
```

## Supported SNES Game Engines

ntrak supports music creation for four major SNES game engines:

- **Super Metroid**: Full feature set with robust echo effects
- **The Legend of Zelda: A Link to the Past**: Command-compatible with Super Metroid (different memory layout/pointers)
- **Super Mario World 2: Yoshi's Island**: Command-compatible with Super Metroid/ALttP (different memory layout/pointers)
- **Super Mario World**: Prototype-style N-SPC variant that requires command remapping

Select your target engine in the Control Panel to access engine-specific features and optimize for the correct memory layout.

### Advanced: Add Compatible Engines

Bundled engine profiles are defined in `config/engine_configs.json`.

User customizations belong in `engine_overrides.json`:
- Linux path: `$XDG_CONFIG_HOME/ntrak/engine_overrides.json` (or `~/.config/ntrak/engine_overrides.json`)
- Format: JSON array of engine objects keyed by `id` (preferred) or `name`
- Behavior: matching engines override bundled fields; unknown ids/names are appended as custom engines

This keeps bundled engine updates (new fields/fixes) flowing automatically while preserving local custom changes.

## Keyboard Shortcuts

### Global
- `Ctrl+S`: Save project
- `Ctrl+Shift+S`: Save project as
- `F5`: Play song from beginning
- `F6`: Play from current pattern
- `F8`: Stop playback
- `Space`: Play/pause toggle

### Pattern Editor
- `Arrow Keys`: Navigate cells
- `Tab/Shift+Tab`: Switch channels
- `ZSXDCVGBHNJM...`: Enter notes (piano keys)
- `.` (period): Enter rest
- `[/]`: Change octave
- `Ctrl+C/X/V`: Copy/cut/paste
- `Ctrl+E`: Open FX editor
- `Ctrl+I`: Interpolate selection
- `Ctrl+Arrow`: Transpose selection
- `Alt+R`: Open song instrument remap popup

See the Quick Guide panel in the application or [USER_GUIDE.md](USER_GUIDE.md) for the complete list.

## Contributing

Contributions are welcome! Please:

1. Review the [STYLE_GUIDE.md](STYLE_GUIDE.md) for coding conventions
2. Ensure your code compiles with C++23 standards
3. Test on your target platform
4. Submit pull requests with clear descriptions

## Dependencies

All dependencies are automatically fetched by CMake except for system libraries:

### Fetched via CMake FetchContent
- [GLFW](https://www.glfw.org/) - Window and input handling
- [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) - User interface
- [miniaudio](https://miniaud.io/) - Audio output
- [Native File Dialog Extended](https://github.com/btzy/nativefiledialog-extended) - File dialogs
- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing
- [GoogleTest](https://github.com/google/googletest) - Testing framework

### Vendored
- [glad](https://glad.dav1d.de/) - OpenGL loader (in `libs/glad`)

### Embedded Core
- [ares-apu](src/emulation/ares-apu/) - Standalone SPC700/S-DSP core extracted from ares (ISC license)

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
Third-party dependency attributions and their license terms are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Acknowledgments

- The ares team and Near et al for the excellent SPC700/S-DSP core used by ares-apu
- The Dear ImGui team for the powerful immediate-mode GUI framework
- The SNES development community for reverse-engineering and documenting the SNES audio system

## Support and Issues

- **User Questions**: Start with [USER_GUIDE.md](USER_GUIDE.md)
- **Bug Reports**: File an issue on the project repository
- **Feature Requests**: Open a discussion or issue with your suggestion

---

Build songs, test often, and use the Quick Guide when you get stuck.
