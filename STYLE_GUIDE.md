# NTRAK C++23 Style Guide

This document defines the coding standards, naming conventions, and development guidelines for the NTRAK project. All new code must follow these rules; existing code should be migrated incrementally.

---

## 1. Language Standard

- Target **C++23**. Use C++23 features where they improve clarity (e.g., `std::expected`, `std::print`, deducing `this`, `std::ranges`, `if consteval`, multidimensional `operator[]`).
- Compile with `-std=c++23` (GCC/Clang) or `/std:c++latest` (MSVC).
- Enable warnings aggressively: `-Wall -Wextra -Wpedantic -Werror` in CI builds.

---

## 2. Project Structure

The project follows a mirrored `include/` and `src/` layout. Header paths mirror namespace paths.

```
NTRAK/
├── include/
│   └── ntrak/
│       ├── app/
│       ├── audio/
│       ├── common/
│       ├── emulation/
│       ├── nspc/          ← NspcEngine.hpp, NspcParser.hpp, NspcProject.hpp
│       └── ui/            ← Panel.hpp, PatternEditorPanel.hpp, ...
├── src/
│   ├── app/
│   ├── audio/
│   ├── common/
│   ├── emulation/
│   ├── nspc/              ← NspcEngine.cpp, NspcParser.cpp, NspcProject.cpp
│   └── ui/                ← PatternEditorPanel.cpp, SpcPlayerPanel.cpp, ...
├── assets/
├── config/
├── libs/                  ← Third-party dependencies
├── CMakeLists.txt
└── main.cpp
```

**Rules:**

- Every header in `include/ntrak/<module>/` has a corresponding source file in `src/<module>/`.
- Do not place implementation details in `include/`. Use anonymous namespaces or `detail` namespaces in `.cpp` files.
- Third-party code lives in `libs/` and is never modified directly.

---

## 3. File Naming

| Type | Convention | Examples |
|---|---|---|
| Headers | `PascalCase.hpp` | `NspcEngine.hpp`, `PatternEditorPanel.hpp` |
| Sources | `PascalCase.cpp` | `NspcEngine.cpp`, `PatternEditorPanel.cpp` |
| Build files | `CMakeLists.txt` | — |
| Config / meta | `lowercase` or `lowercase.ext` | `.gitignore`, `imgui.ini` |

- Use `.hpp` for C++ headers, never `.h` (reserve `.h` for C interop if ever needed).
- Use `.cpp` for source files.
- File names must match the primary class or module they define: `NspcParser.hpp` defines `NspcParser`.

---

## 4. Naming Conventions

### 4.1 General Rules

| Element | Style | Examples |
|---|---|---|
| Namespaces | `snake_case` | `ntrak`, `ntrak::nspc`, `ntrak::ui` |
| Classes / Structs | `PascalCase` | `NspcEngine`, `PatternEditorPanel` |
| Enums (scoped) | `PascalCase` | `enum class ChannelState` |
| Enum values | `PascalCase` | `ChannelState::Active`, `ChannelState::Muted` |
| Functions / Methods | `camelCase` | `parsePattern()`, `getVolume()` |
| Variables (local) | `camelCase` | `sampleRate`, `channelCount` |
| Member variables | `camelCase` + trailing `_` | `engine_`, `isPlaying_` |
| Static member variables | `camelCase` + trailing `_` | `instance_` |
| Constants (`constexpr`) | `k` prefix + `PascalCase` | `kMaxChannels`, `kDefaultTempo` |
| Global constants | `k` prefix + `PascalCase` | `kVersion` |
| Macros (avoid) | `SCREAMING_SNAKE_CASE` | `NTRAK_ASSERT(x)` |
| Template parameters | `PascalCase` | `template <typename SampleType>` |
| Concepts | `PascalCase` | `concept AudioSource` |
| Type aliases | `PascalCase` | `using SampleBuffer = std::vector<float>;` |

### 4.2 Prefixes and Suffixes

- **Boolean variables and functions:** Use `is`, `has`, `can`, `should` prefixes. Example: `isPlaying_`, `hasData()`, `canSeek()`.
- **Getters:** Name after the property, no `get` prefix unless ambiguity exists. Example: `volume()` not `getVolume()`, but `getChannelState(int ch)` is acceptable when parameters make bare names unclear.
- **Setters:** Use `set` prefix. Example: `setVolume(float v)`.
- **Factory functions:** Use `create` or `make` prefix. Example: `createEngine()`, `makeParser()`.

### 4.3 Abbreviations

- Established domain abbreviations are fine: `Spc`, `Nspc`, `Dsp`, `Brr`, `Apu`.
- Do not invent new abbreviations. Prefer `PatternEditorPanel` over `PatEdPnl`.
- Treat abbreviations as words for casing: `NspcEngine`, not `NSPCEngine`.

---

## 5. Formatting

### 5.1 Indentation and Braces

- Use **4 spaces** for indentation. No tabs.
- Opening braces on the **same line** (Allman style is not used).
- Always use braces for control structures, even single-line bodies.

```cpp
if (isPlaying_) {
    stop();
}

for (auto& channel : channels_) {
    channel.reset();
}

namespace ntrak::nspc {

class NspcEngine {
public:
    void play();

private:
    bool isPlaying_ = false;
};

} // namespace ntrak::nspc
```

### 5.2 Line Length

- Soft limit: **100 columns**.
- Hard limit: **120 columns**.
- Break long lines at logical points (after commas, before operators).

### 5.3 Spacing

- One space after control keywords: `if (`, `for (`, `while (`, `switch (`.
- No space after function names: `parsePattern(data)`.
- Spaces around binary operators: `a + b`, `x == y`.
- No spaces inside parentheses: `foo(a, b)` not `foo( a, b )`.
- No trailing whitespace.

### 5.4 Blank Lines

- One blank line between function definitions.
- One blank line between logical sections within a function (use sparingly).
- Two blank lines between major sections of a file (e.g., between the include block and namespace opening).
- No multiple consecutive blank lines.

### 5.5 Include Order

Group includes in the following order, separated by blank lines:

```cpp
// 1. Corresponding header (for .cpp files)
#include "ntrak/nspc/NspcParser.hpp"

// 2. Project headers
#include "ntrak/common/Types.hpp"
#include "ntrak/nspc/NspcEngine.hpp"

// 3. Third-party library headers
#include <imgui.h>
#include <spdlog/spdlog.h>

// 4. Standard library headers
#include <cstdint>
#include <span>
#include <string>
#include <vector>
```

- Within each group, sort alphabetically.
- **Exception: `<glad/glad.h>` must always be the very first include** in any file that uses OpenGL. GLAD provides its own GL declarations and will error if a system GL header is included first.
- Use `""` for project headers, `<>` for system and third-party headers.
- Never use `using namespace` in headers.

---

## 6. Header Files

### 6.1 Header Guards

Use `#pragma once`. It is supported by all target compilers (GCC, Clang, MSVC).

```cpp
#pragma once

#include <cstdint>

namespace ntrak::nspc {
// ...
} // namespace ntrak::nspc
```

### 6.2 Forward Declarations

- Prefer forward declarations over includes when only pointers or references are used.
- If a header only needs a type for a function signature, forward-declare it.

### 6.3 What Belongs in Headers

- Class declarations, inline/template function definitions, constants, type aliases, concepts.
- Do **not** put non-template function bodies in headers unless they are trivial one-liners marked `inline`.

---

## 7. Classes and Structs

### 7.1 When to Use Which

- **`struct`**: Plain data aggregates with public members, no invariants. Example: `struct Color { uint8_t r, g, b, a; };`
- **`class`**: Types with invariants, private state, and member functions.

### 7.2 Member Order

Declare members in this order within each access specifier:

```cpp
class NspcEngine {
public:
    // 1. Type aliases and nested types
    using Callback = std::function<void()>;

    // 2. Constructors, destructor, assignment
    NspcEngine();
    ~NspcEngine();
    NspcEngine(const NspcEngine&) = delete;
    NspcEngine& operator=(const NspcEngine&) = delete;

    // 3. Public member functions
    void play();
    void stop();
    bool isPlaying() const;

protected:
    // (if needed)

private:
    // 4. Private member functions
    void advanceTick();

    // 5. Data members (always last)
    bool isPlaying_ = false;
    int tempo_ = 120;
    std::vector<Channel> channels_;
};
```

### 7.3 Special Member Functions

- Follow the **Rule of Five/Zero**. Either define all five special member functions or none.
- Explicitly `= delete` copy operations for non-copyable types.
- Prefer `= default` when the compiler-generated version is correct.

### 7.4 Initialization

- Use **in-class member initializers** as the default.
- Use constructor initializer lists for members that depend on constructor arguments.

```cpp
class Player {
public:
    explicit Player(int sampleRate)
        : sampleRate_(sampleRate) {}  // depends on arg

private:
    int sampleRate_;
    bool isPlaying_ = false;      // default via in-class init
    float volume_ = 1.0f;         // default via in-class init
};
```

---

## 8. Modern C++ Practices

### 8.1 Ownership and Memory

- **No raw `new`/`delete`**. Use `std::unique_ptr` for exclusive ownership, `std::shared_ptr` only when shared ownership is genuinely required.
- Use `std::make_unique` and `std::make_shared`.
- Pass non-owning access as references or `std::span`.
- Prefer value semantics where practical.

### 8.2 Type Safety

- Use `enum class` (scoped enums), never unscoped enums.
- Use `std::byte` for raw byte data, not `char` or `unsigned char`.
- Use fixed-width integers (`uint8_t`, `int16_t`, `uint32_t`) for hardware-facing code (emulation, audio, NSPC). Use `int` / `size_t` for general-purpose code.
- Use `std::optional` for values that may not exist.
- Use `std::expected` (C++23) for operations that can fail, instead of error codes or exceptions.
- Use `std::variant` instead of unions.

### 8.3 `auto` Usage

- Use `auto` when the type is obvious from the right-hand side or for iterators.
- Do **not** use `auto` when the type is not clear from context.

```cpp
auto engine = std::make_unique<NspcEngine>();   // ✓ obvious
auto it = channels_.begin();                     // ✓ iterator
auto result = parseHeader(data);                 // ✗ unclear — spell out the type
ParseResult result = parseHeader(data);          // ✓ clear
```

### 8.4 `const` and `constexpr`

- Use `const` by default for variables that don't change.
- Use `constexpr` for compile-time constants and functions where possible.
- Use `consteval` for functions that must only run at compile time.
- Mark member functions `const` if they don't modify state.
- Prefer `const` references for function parameters that are not modified: `void process(const std::vector<uint8_t>& data)`.

### 8.5 Range-Based Constructs

- Prefer range-based `for` loops over index-based loops.
- Use `std::ranges` algorithms over `<algorithm>` iterator pairs.

```cpp
// Prefer this:
for (const auto& channel : channels_) { ... }

// Over this:
for (size_t i = 0; i < channels_.size(); ++i) { ... }

// Use ranges:
auto active = channels_ | std::views::filter(&Channel::isActive);
```

### 8.6 Structured Bindings

Use structured bindings for pairs, tuples, and simple structs:

```cpp
auto [offset, length] = parseBlock(data);
for (const auto& [key, value] : settings_) { ... }
```

### 8.7 Lambdas

- Prefer lambdas over `std::bind`.
- Keep lambdas short. If a lambda exceeds ~15 lines, extract it into a named function.
- Capture by reference `[&]` in local scopes; capture explicitly for stored lambdas.

### 8.8 Casts

- Never use C-style casts: `(int)x`.
- Use `static_cast`, `reinterpret_cast`, `std::bit_cast` (C++20/23) as appropriate.
- Prefer `std::bit_cast` over `reinterpret_cast` for type-punning.

---

## 9. Error Handling

- Use `std::expected<T, E>` (C++23) as the primary error-handling mechanism for operations that can fail. This keeps the codebase exception-free in hot paths.
- Reserve exceptions for truly exceptional, unrecoverable situations (e.g., out of memory).
- Use `assert()` or a project-specific `NTRAK_ASSERT()` macro for invariant checking in debug builds.
- Never silently swallow errors.

```cpp
// Preferred:
std::expected<NspcProject, ParseError> loadProject(std::span<const std::byte> data);

// Usage:
auto result = loadProject(fileData);
if (!result) {
    spdlog::error("Failed to load project: {}", result.error().message());
    return;
}
auto& project = *result;
```

---

## 10. Namespaces

- All project code lives under the `ntrak` namespace.
- Use nested namespaces matching the directory structure: `ntrak::nspc`, `ntrak::ui`, `ntrak::audio`, `ntrak::emulation`, `ntrak::app`.
- Use the compact C++17 nested namespace syntax.
- Close namespaces with a comment.

```cpp
namespace ntrak::nspc {

// ...

} // namespace ntrak::nspc
```

- Use anonymous namespaces in `.cpp` files for internal linkage (instead of `static`).
- Never use `using namespace` in header files. In `.cpp` files, it is acceptable only for `ntrak` sub-namespaces.

---

## 11. Comments and Documentation

### 11.1 General

- Write code that is self-documenting via clear naming. Use comments to explain **why**, not **what**.
- Do not leave commented-out code in the codebase. Use version control.
- Remove TODO/FIXME comments once addressed, or tag them with a tracking issue: `// TODO(#42): Support stereo BRR samples`.

### 11.2 Documentation Comments

Use Doxygen-style `///` comments for public API:

```cpp
/// Parses an NSPC sequence from raw SPC data.
///
/// @param data  Raw SPC file contents.
/// @return Parsed project on success, or a ParseError describing the failure.
std::expected<NspcProject, ParseError> parseSequence(std::span<const std::byte> data);
```

- Document all public classes, functions, and non-obvious parameters.
- Internal/private functions need comments only if the logic is non-obvious.

---

## 12. Templates and Concepts

- Use C++20 concepts to constrain templates instead of `static_assert` or SFINAE.
- Define concepts in dedicated headers or alongside the types they constrain.

```cpp
template <typename T>
concept AudioSource = requires(T source, std::span<float> buffer) {
    { source.fillBuffer(buffer) } -> std::same_as<size_t>;
    { source.sampleRate() } -> std::convertible_to<int>;
};

template <AudioSource Source>
void renderAudio(Source& source, std::span<float> output);
```

---

## 13. Concurrency

- Prefer `std::jthread` over `std::thread`.
- Use `std::atomic` for lock-free shared state (e.g., audio thread ↔ UI thread communication).
- Clearly document thread-safety guarantees in class documentation.
- Keep shared mutable state to an absolute minimum.

---

## 14. CMake Guidelines

- Minimum CMake version: **3.25** (for C++23 module support readiness).
- Use `target_*` commands, never directory-level `include_directories()` or `add_definitions()`.
- Set the standard per-target:

```cmake
target_compile_features(ntrak_nspc PUBLIC cxx_std_23)
```

- Each module (`app`, `nspc`, `ui`, etc.) has its own `CMakeLists.txt` defining a library target.
- Express dependencies via `target_link_libraries`. Do not rely on transitive include paths.

---

## 15. Testing (if applicable)

- Place tests in a `tests/` directory mirroring the `src/` structure.
- Test file naming: `Test<ClassName>.cpp` (e.g., `TestNspcParser.cpp`).
- Prefer small, focused tests. Test public API; avoid testing implementation details.

---

## 16. Git and Workflow

- Commit messages: imperative mood, concise summary line ≤ 72 characters. Example: `Fix BRR sample loop point calculation`
- One logical change per commit.
- Keep `.gitignore` up to date. Never commit build artifacts, IDE-specific files, or generated outputs.

---

## 17. Quick Reference Card

```
Files:           PascalCase.hpp / PascalCase.cpp
Classes:         PascalCase
Functions:       camelCase()
Variables:       camelCase
Members:         camelCase_  (trailing underscore)
Static Members:  camelCase_  (trailing underscore)
Constants:       kPascalCase
Enums:           enum class PascalCase { ValueName }
Namespaces:      snake_case (ntrak::nspc)
Macros:          SCREAMING_SNAKE_CASE (avoid macros)
Indent:          4 spaces, no tabs
Line limit:      100 soft / 120 hard
Braces:          Same-line opening
Headers:         #pragma once, .hpp extension
Ownership:       unique_ptr by default, no raw new/delete
Errors:          std::expected<T, E>
```
