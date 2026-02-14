// ares-apu: Standalone SNES APU library
// Adapted from the ares emulator (ISC License)

#include "../include/ares-apu.h"
#include "smp.h"
#include "dsp.h"

#include <cstring>

static constexpr uint32_t CPUK_TICKS_PER_DSP_SAMPLE = 64;

struct AresAPU::Impl {
  DSP dsp;
  SMP smp;
  AresAPU::MemoryAccessCallback memoryAccessCallback = nullptr;
  void* memoryAccessUserdata = nullptr;

  static void onSmpMemoryAccess(uint8_t access, uint16_t address, uint8_t value, uint64_t cycle, uint16_t pc,
                                bool isDummy, void* userdata) {
    auto* self = static_cast<Impl*>(userdata);
    if(!self || !self->memoryAccessCallback) return;
    self->memoryAccessCallback((AresAPU::MemoryAccessType)access, address, value, cycle, pc, isDummy,
                               self->memoryAccessUserdata);
  }
};

AresAPU::AresAPU() {
  impl = new Impl;
  impl->smp.dsp = &impl->dsp;
  impl->smp.memoryAccessCallback = &Impl::onSmpMemoryAccess;
  impl->smp.memoryAccessUserdata = impl;
}

AresAPU::~AresAPU() {
  delete impl;
}

// Minimal IPL ROM: jumps to $0200 and halts
static const uint8_t defaultIPL[64] = {
  // This is a minimal program that:
  // Sets up the stack and jumps to $0200
  // $FFC0: MOV X,#$EF      ; CD EF
  // $FFC2: MOV SP,X         ; BD
  // $FFC3: MOV A,#$00       ; E8 00
  // $FFC5: MOV (X)+,A       ; AF
  // $FFC6: CMP X,#$00       ; C8 00
  // $FFC8: BNE $FFC5        ; D0 FB
  // $FFCA: MOV $F1,#$30     ; 8F 30 F1   ; clear IO ports
  // $FFCD: MOV $FC,#$FF     ; 8F FF FC   ; timer2 target = 255
  // $FFD0: JMP $0200        ; 5F 00 02
  // pad rest with NOPs and set reset vector
  0xCD, 0xEF,                   // MOV X,#$EF
  0xBD,                         // MOV SP,X
  0xE8, 0x00,                   // MOV A,#$00
  0xAF,                         // MOV (X)+,A
  0xC8, 0x00,                   // CMP X,#$00
  0xD0, 0xFB,                   // BNE $FFC5
  0x8F, 0x30, 0xF1,             // MOV $F1,#$30
  0x8F, 0xFF, 0xFC,             // MOV $FC,#$FF
  0x5F, 0x00, 0x02,             // JMP $0200
  // Pad with NOPs (0x00) to fill 64 bytes
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00,
  0xC0, 0xFF,                   // Reset vector: $FFC0
};

void AresAPU::reset(const uint8_t iplrom[64], bool preserveRAM) {
  impl->dsp.power(preserveRAM);

  if(iplrom) {
    memcpy(impl->smp.iplrom, iplrom, 64);
  } else {
    memcpy(impl->smp.iplrom, defaultIPL, 64);
  }

  impl->smp.power(preserveRAM);
  impl->smp.memoryAccessCallback = &Impl::onSmpMemoryAccess;
  impl->smp.memoryAccessUserdata = impl;
}

AresAPU::StereoSample AresAPU::step() {
  // Run the SMP until we've accumulated one DSP output sample worth of CPUK ticks.
  // IMPORTANT: do NOT reset cycleCounter each call; carry the remainder.
  while (impl->smp.cycleCounter < CPUK_TICKS_PER_DSP_SAMPLE) {
    impl->smp.main();
  }
  impl->smp.cycleCounter -= CPUK_TICKS_PER_DSP_SAMPLE;

  // In this DSP port, DSP::main() produces exactly one output sample (sets sampleLeft/Right).
  impl->dsp.sampleReady = false;
  impl->dsp.main();

  StereoSample result;
  result.left  = impl->dsp.sampleLeft;
  result.right = impl->dsp.sampleRight;
  return result;
}

AresAPU::StereoSample AresAPU::stepDSPOnly() {
  impl->dsp.sampleReady = false;

  // Advance only DSP state (voices/echo/envelopes/noise), leaving SPC700 frozen.
  impl->dsp.main();

  StereoSample result;
  result.left = impl->dsp.sampleLeft;
  result.right = impl->dsp.sampleRight;
  return result;
}

void AresAPU::writePort(int port, uint8_t data) {
  if(port >= 0 && port <= 3) {
    impl->smp.portWrite(port, data);
  }
}

uint8_t AresAPU::readPort(int port) {
  if(port >= 0 && port <= 3) {
    return impl->smp.portRead(port);
  }
  return 0;
}

uint8_t* AresAPU::ram() {
  return (uint8_t*)impl->dsp.apuram;
}

const uint8_t* AresAPU::ram() const {
  return (const uint8_t*)impl->dsp.apuram;
}

void AresAPU::writeDSP(uint8_t address, uint8_t data) {
  impl->dsp.write(address & 0x7f, data);
}

uint8_t AresAPU::readDSP(uint8_t address) {
  return impl->dsp.read(address & 0x7f);
}

void AresAPU::setPC(uint16_t address) {
  impl->smp.r.pc = address;
}

uint16_t AresAPU::getPC() const {
  return impl->smp.r.pc;
}

uint8_t AresAPU::getA() const {
  return impl->smp.r.a;
}

uint8_t AresAPU::getX() const {
  return impl->smp.r.x;
}

uint8_t AresAPU::getY() const {
  return impl->smp.r.y;
}

uint8_t AresAPU::getSP() const {
  return impl->smp.r.s;
}

uint8_t AresAPU::getPS() const {
  return impl->smp.r.p;
}

void AresAPU::setA(uint8_t value) {
  impl->smp.r.a = value;
}

void AresAPU::setX(uint8_t value) {
  impl->smp.r.x = value;
}

void AresAPU::setY(uint8_t value) {
  impl->smp.r.y = value;
}

void AresAPU::setSP(uint8_t value) {
  impl->smp.r.s = value;
}

void AresAPU::setPS(uint8_t value) {
  impl->smp.r.p = value;
}

void AresAPU::writeSMPIO(uint8_t address, uint8_t value) {
  // Write directly to SMP I/O - this properly updates internal state
  impl->smp.writeIO(0x00F0 + (address & 0x0F), value);
}

bool AresAPU::muted() const {
  return impl->dsp.mute();
}

void AresAPU::setExecHook(ExecCallback callback, void* userdata) {
  impl->smp.execCallback = callback;
  impl->smp.execUserdata = userdata;
}

void AresAPU::setMemoryAccessHook(MemoryAccessCallback callback, void* userdata) {
  impl->memoryAccessCallback = callback;
  impl->memoryAccessUserdata = userdata;
}

void AresAPU::addBreakpoint(uint16_t address) {
  impl->smp.breakpoints[address] = true;
}

void AresAPU::removeBreakpoint(uint16_t address) {
  impl->smp.breakpoints[address] = false;
}

void AresAPU::clearBreakpoints() {
  memset(impl->smp.breakpoints, 0, sizeof(impl->smp.breakpoints));
}

void AresAPU::setChannelMask(uint8_t mask) {
  impl->dsp.channelMask = mask;
}

uint8_t AresAPU::getChannelMask() const {
  return impl->dsp.channelMask;
}

void AresAPU::muteChannel(int channel, bool mute) {
  if(channel < 0 || channel > 7) return;
  if(mute) {
    impl->dsp.channelMask &= ~(1 << channel);
  } else {
    impl->dsp.channelMask |= (1 << channel);
  }
}

bool AresAPU::isChannelMuted(int channel) const {
  if(channel < 0 || channel > 7) return true;
  return !(impl->dsp.channelMask & (1 << channel));
}
