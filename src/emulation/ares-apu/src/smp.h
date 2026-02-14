#pragma once
// SNES SMP (Sony CXP1100Q-1) - adapted from ares/sfc/smp/
// Original: Copyright (c) 2004-2025 ares team, Near et al (ISC License)

#include "spc700.h"

struct DSP;

struct SMP : SPC700 {
  auto synchronizing() const -> bool override { return false; }

  //smp.cpp
  auto main() -> void;
  auto power(bool reset) -> void;

  //io.cpp
  auto portRead(u32 port) const -> n8;
  auto portWrite(u32 port, n8 data) -> void;
  
  //io.cpp - public for SPC file loading
  auto readIO(n16 address) -> n8;
  auto writeIO(n16 address, n8 data) -> void;

  n8 iplrom[64];

  // Pointer to DSP (set by AresAPU)
  DSP* dsp = nullptr;

  // Cycle counter for DSP synchronization
  u32 cycleCounter = 0;
  uint64_t globalCycleCounter = 0;

  // Execution hooks
  using ExecCallback = void(*)(uint16_t pc, void* userdata);
  ExecCallback execCallback = nullptr;
  void* execUserdata = nullptr;
  bool breakpoints[65536] = {};

  // Memory access hook (execute/read/write)
  using MemoryAccessCallback = void(*)(uint8_t access, uint16_t address, uint8_t value, uint64_t cycle, uint16_t pc,
                                       bool isDummy, void* userdata);
  MemoryAccessCallback memoryAccessCallback = nullptr;
  void* memoryAccessUserdata = nullptr;

private:
  struct IO {
    //timing
    u32 clockCounter = 0;
    u32 dspCounter = 0;

    //external
    n8 apu0;
    n8 apu1;
    n8 apu2;
    n8 apu3;

    //$00f0
    n1 timersDisable;
    n1 ramWritable = true;
    n1 ramDisable;
    n1 timersEnable = true;
    n2 externalWaitStates;
    n2 internalWaitStates;

    //$00f1
    n1 iplromEnable = true;

    //$00f2
    n8 dspAddress;

    //$00f4-00f7
    n8 cpu0;
    n8 cpu1;
    n8 cpu2;
    n8 cpu3;

    //$00f8-00f9
    n8 aux4;
    n8 aux5;
  } io;

  //memory.cpp
  auto readRAM(n16 address) -> n8;
  auto writeRAM(n16 address, n8 data) -> void;

  auto idle() -> void override;
  auto read(n16 address, BusAccessType type = BusAccessType::Read) -> n8 override;
  auto write(n16 address, n8 data, BusAccessType type = BusAccessType::Write) -> void override;

  template<u32 Frequency>
  struct Timer {
    n8 stage0;
    n8 stage1;
    n8 stage2;
    n4 stage3;
    b1 line;
    b1 enable;
    n8 target;

    //timing.cpp
    auto step(u32 clocks, bool timersEnable, bool timersDisable) -> void;
    auto synchronizeStage1(bool timersEnable, bool timersDisable) -> void;
  };

  Timer<128> timer0;
  Timer<128> timer1;
  Timer< 16> timer2;

  //timing.cpp
  auto wait(bool halve, bool hasAddress = false, n16 address = 0) -> void;
  auto step(u32 clocks) -> void;
  auto stepTimers(u32 clocks) -> void;
};
