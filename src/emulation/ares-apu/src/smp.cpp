// SNES SMP implementation - adapted from ares/sfc/smp/
// Original: Copyright (c) 2004-2025 ares team, Near et al (ISC License)

#include <cstdint>

#include "smp.h"
#include "dsp.h"

//=== memory.cpp ===

inline auto SMP::readRAM(n16 address) -> n8 {
  if(address >= 0xffc0 && io.iplromEnable) return iplrom[(u64)address & 0x3f];
  if(io.ramDisable) return 0x5a;
  return dsp->apuram[address];
}

inline auto SMP::writeRAM(n16 address, n8 data) -> void {
  if(io.ramWritable && !io.ramDisable) dsp->apuram[address] = data;
}

auto SMP::idle() -> void {
  // wait(0);
  // internal cycle: no address, so internal wait states apply
  static constexpr u32 cycleWaitStates[4] = {2, 4, 10, 20};
  static constexpr u32 timerWaitStates[4] = {2, 4,  8, 16};

  const u32 ws = io.internalWaitStates;

  step(cycleWaitStates[ws]);       // CPUK clocks
  stepTimers(timerWaitStates[ws]); // timer clocks
}

auto SMP::read(n16 address, BusAccessType type) -> n8 {
  const bool isDummy = type == BusAccessType::DummyRead || type == BusAccessType::DummyWrite;
  uint8_t access = 1;  // read
  if(type == BusAccessType::Execute) {
    access = 0;
  } else if(type == BusAccessType::Write || type == BusAccessType::DummyWrite) {
    access = 2;
  }

  if(((u64)address & 0xfffc) == 0x00f4) {
    wait(1, true, address);
    n8 data = readRAM(address);
    if(((u64)address & 0xfff0) == 0x00f0) data = readIO(address);
    wait(1, true, address);
    if(memoryAccessCallback) {
      const uint16_t pc = type == BusAccessType::Execute ? (uint16_t)address : (uint16_t)r.pc;
      memoryAccessCallback(access, (uint16_t)address, (uint8_t)data, globalCycleCounter, pc, isDummy,
                           memoryAccessUserdata);
    }
    return data;
  } else {
    wait(0, true, address);
    n8 data = readRAM(address);
    if(((u64)address & 0xfff0) == 0x00f0) data = readIO(address);
    if(memoryAccessCallback) {
      const uint16_t pc = type == BusAccessType::Execute ? (uint16_t)address : (uint16_t)r.pc;
      memoryAccessCallback(access, (uint16_t)address, (uint8_t)data, globalCycleCounter, pc, isDummy,
                           memoryAccessUserdata);
    }
    return data;
  }
}

auto SMP::write(n16 address, n8 data, BusAccessType type) -> void {
  const bool isDummy = type == BusAccessType::DummyRead || type == BusAccessType::DummyWrite;
  uint8_t access = 2;  // write
  if(type == BusAccessType::Execute) {
    access = 0;
  } else if(type == BusAccessType::Read || type == BusAccessType::DummyRead) {
    access = 1;
  }

  wait(0, true, address);
  writeRAM(address, data);
  if(((u64)address & 0xfff0) == 0x00f0) writeIO(address, data);
  if(memoryAccessCallback) {
    memoryAccessCallback(access, (uint16_t)address, (uint8_t)data, globalCycleCounter, (uint16_t)r.pc, isDummy,
                         memoryAccessUserdata);
  }
}

//=== io.cpp ===

auto SMP::portRead(u32 port) const -> n8 {
  if(port == 0) return io.cpu0;
  if(port == 1) return io.cpu1;
  if(port == 2) return io.cpu2;
  if(port == 3) return io.cpu3;
  return 0;
}

auto SMP::portWrite(u32 port, n8 data) -> void {
  if(port == 0) io.apu0 = data;
  if(port == 1) io.apu1 = data;
  if(port == 2) io.apu2 = data;
  if(port == 3) io.apu3 = data;
}

inline auto SMP::readIO(n16 address) -> n8 {
  n8 data;

  switch((u64)address) {
  case 0xf0:  //TEST (write-only)
    return 0x00;

  case 0xf1:  //CONTROL (write-only)
    return 0x00;

  case 0xf2:  //DSPADDR
    return io.dspAddress;

  case 0xf3:  //DSPDATA
    return dsp->read(io.dspAddress);

  case 0xf4:  //CPUIO0
    return io.apu0;

  case 0xf5:  //CPUIO1
    return io.apu1;

  case 0xf6:  //CPUIO2
    return io.apu2;

  case 0xf7:  //CPUIO3
    return io.apu3;

  case 0xf8:  //AUXIO4
    return io.aux4;

  case 0xf9:  //AUXIO5
    return io.aux5;

  case 0xfa:  //T0TARGET
  case 0xfb:  //T1TARGET
  case 0xfc:  //T2TARGET (write-only)
    return 0x00;

  case 0xfd:  //T0OUT
    data = timer0.stage3;
    timer0.stage3 = 0;
    return data;

  case 0xfe:  //T1OUT
    data = timer1.stage3;
    timer1.stage3 = 0;
    return data;

  case 0xff:  //T2OUT
    data = timer2.stage3;
    timer2.stage3 = 0;
    return data;
  }

  return data;
}

inline auto SMP::writeIO(n16 address, n8 data) -> void {
  switch((u64)address) {
  case 0xf0:  //TEST
    if(r.p.p) break;

    io.timersDisable      = data.bit(0);
    io.ramWritable        = data.bit(1);
    io.ramDisable         = data.bit(2);
    io.timersEnable       = data.bit(3);
    io.externalWaitStates = data.bit(4,5);
    io.internalWaitStates = data.bit(6,7);

    timer0.synchronizeStage1(io.timersEnable, io.timersDisable);
    timer1.synchronizeStage1(io.timersEnable, io.timersDisable);
    timer2.synchronizeStage1(io.timersEnable, io.timersDisable);
    break;

  case 0xf1:  //CONTROL
    if(timer0.enable.raise(data.bit(0))) {
      timer0.stage2 = 0;
      timer0.stage3 = 0;
    }

    if(timer1.enable.raise(data.bit(1))) {
      timer1.stage2 = 0;
      timer1.stage3 = 0;
    }

    if(!timer2.enable.raise(data.bit(2))) {
      timer2.stage2 = 0;
      timer2.stage3 = 0;
    }

    if(data.bit(4)) {
      io.apu0 = 0x00;
      io.apu1 = 0x00;
    }

    if(data.bit(5)) {
      io.apu2 = 0x00;
      io.apu3 = 0x00;
    }

    io.iplromEnable = data.bit(7);
    break;

  case 0xf2:  //DSPADDR
    io.dspAddress = data;
    break;

  case 0xf3:  //DSPDATA
    if(io.dspAddress.bit(7)) break;
    dsp->write(io.dspAddress, data);
    break;

  case 0xf4:  //CPUIO0
    io.cpu0 = data;
    break;

  case 0xf5:  //CPUIO1
    io.cpu1 = data;
    break;

  case 0xf6:  //CPUIO2
    io.cpu2 = data;
    break;

  case 0xf7:  //CPUIO3
    io.cpu3 = data;
    break;

  case 0xf8:  //AUXIO4
    io.aux4 = data;
    break;

  case 0xf9:  //AUXIO5
    io.aux5 = data;
    break;

  case 0xfa:  //T0TARGET
    timer0.target = data;
    break;

  case 0xfb:  //T1TARGET
    timer1.target = data;
    break;

  case 0xfc:  //T2TARGET
    timer2.target = data;
    break;

  case 0xfd:  //T0OUT
  case 0xfe:  //T1OUT
  case 0xff:  //T2OUT (read-only)
    break;
  }
}

//=== timing.cpp ===

inline auto SMP::wait(bool halve, bool hasAddress, n16 address) -> void {
  static constexpr u32 cycleWaitStates[4] = {2, 4, 10, 20};
  static constexpr u32 timerWaitStates[4] = {2, 4,  8, 16};

  u32 waitStates = io.externalWaitStates;
  if(!hasAddress) waitStates = io.internalWaitStates;  //idle cycles
  else if(((u64)address & 0xfff0) == 0x00f0) waitStates = io.internalWaitStates;  //IO registers
  else if((u64)address >= 0xffc0 && io.iplromEnable) waitStates = io.internalWaitStates;  //IPLROM

  u32 cycleShift = halve ? 1 : 0;
  step(cycleWaitStates[waitStates] >> cycleShift);
  stepTimers(timerWaitStates[waitStates] >> cycleShift);
}

inline auto SMP::step(u32 clocks) -> void {
  cycleCounter += clocks;
  globalCycleCounter += clocks;
}

inline auto SMP::stepTimers(u32 clocks) -> void {
  timer0.step(clocks, io.timersEnable, io.timersDisable);
  timer1.step(clocks, io.timersEnable, io.timersDisable);
  timer2.step(clocks, io.timersEnable, io.timersDisable);
}

template<u32 Frequency> auto SMP::Timer<Frequency>::step(u32 clocks, bool timersEnable, bool timersDisable) -> void {
  stage0 += clocks;
  if(stage0 < Frequency) return;
  stage0 -= Frequency;

  stage1 ^= 1;
  synchronizeStage1(timersEnable, timersDisable);
}

template<u32 Frequency> auto SMP::Timer<Frequency>::synchronizeStage1(bool timersEnable, bool timersDisable) -> void {
  bool level = stage1;
  if(!timersEnable) level = false;
  if(timersDisable) level = false;
  if(!line.lower(level)) return;

  if(!enable) return;
  if(++stage2 != target) return;

  stage2 = 0;
  stage3++;
}

// Explicit template instantiations
template struct SMP::Timer<128>;
template struct SMP::Timer<16>;

//=== smp.cpp (main, power) ===

auto SMP::main() -> void {
  if(r.wait) return instructionWait();
  if(r.stop) return instructionStop();

  if(execCallback && breakpoints[(u16)r.pc]) {
    execCallback((u16)r.pc, execUserdata);
  }

  instruction();
}

auto SMP::power(bool reset) -> void {
  SPC700::power();

  r.pc = iplrom[62] | (iplrom[63] << 8);

  io = {};
  timer0 = {};
  timer1 = {};
  timer2 = {};
  cycleCounter = 0;
  globalCycleCounter = 0;
  execCallback = nullptr;
  execUserdata = nullptr;
  memoryAccessCallback = nullptr;
  memoryAccessUserdata = nullptr;
  memset(breakpoints, 0, sizeof(breakpoints));
}
