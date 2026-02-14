#pragma once
// ares-apu: Standalone SNES APU (SPC700 + DSP) library
// Extracted from the ares emulator (ISC License)
// Original: Copyright (c) 2004-2025 ares team, Near et al

#include <cstdint>

class AresAPU {
public:
  AresAPU();
  ~AresAPU();

  // Initialize/reset the APU. Optionally provide the 64-byte IPL ROM.
  // If nullptr, uses a built-in minimal IPL that jumps to $0200.
  // If preserveRAM is true, doesn't clear APU RAM during reset.
  void reset(const uint8_t iplrom[64] = nullptr, bool preserveRAM = false);

  // Run the APU for one sample (1/32040th of a second).
  // Returns a stereo sample pair (left, right) as signed 16-bit.
  struct StereoSample { int16_t left, right; };
  StereoSample step();

  // Run DSP only for one sample without advancing SPC700 execution.
  // Useful for note preview paths that directly poke DSP registers.
  StereoSample stepDSPOnly();

  // Write to CPU I/O ports (the 4 ports at $2140-$2143 from CPU side)
  void writePort(int port, uint8_t data);

  // Read from CPU I/O ports
  uint8_t readPort(int port);

  // Direct access to the 64KB APU RAM (for loading SPC programs, samples, etc.)
  uint8_t* ram();
  const uint8_t* ram() const;

  // Direct DSP register access (128 registers, $00-$7F)
  void writeDSP(uint8_t address, uint8_t data);
  uint8_t readDSP(uint8_t address);

  // Set the SMP program counter (useful for starting execution at a specific address)
  void setPC(uint16_t address);

  // Get/set SMP registers for full control
  uint16_t getPC() const;
  uint8_t getA() const;
  uint8_t getX() const;
  uint8_t getY() const;
  uint8_t getSP() const;
  uint8_t getPS() const;
  
  void setA(uint8_t value);
  void setX(uint8_t value);
  void setY(uint8_t value);
  void setSP(uint8_t value);
  void setPS(uint8_t value);
  
  // Write to SMP I/O registers (for proper SPC file loading)
  void writeSMPIO(uint8_t address, uint8_t value);

  // Mute state
  bool muted() const;

  // Execution hooks — fire a callback when SPC700 execution reaches a breakpoint address
  using ExecCallback = void(*)(uint16_t pc, void* userdata);
  void setExecHook(ExecCallback callback, void* userdata = nullptr);
  void addBreakpoint(uint16_t address);
  void removeBreakpoint(uint16_t address);
  void clearBreakpoints();

  enum class MemoryAccessType : uint8_t {
    Execute = 0,
    Read = 1,
    Write = 2
  };

  // Memory access hooks - callback on execute/read/write bus accesses.
  // isDummy is true for timing-only accesses.
  using MemoryAccessCallback = void(*)(MemoryAccessType access, uint16_t address, uint8_t value, uint64_t cycle,
                                       uint16_t pc, bool isDummy, void* userdata);
  void setMemoryAccessHook(MemoryAccessCallback callback, void* userdata = nullptr);

  // Per-channel muting — mute/unmute individual DSP voices (0-7)
  void setChannelMask(uint8_t mask);       // bit N = voice N enabled (0xFF = all on)
  uint8_t getChannelMask() const;
  void muteChannel(int channel, bool mute);
  bool isChannelMuted(int channel) const;

private:
  struct Impl;
  Impl* impl;
};
