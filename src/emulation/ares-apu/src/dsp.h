#pragma once
// SNES DSP (Sony CXD1222Q-1) - adapted from ares/sfc/dsp/
// Original: Copyright (c) 2004-2025 ares team, Near et al (ISC License)

#include "types.h"

struct DSP {
  u8 apuram[65536];
  u8 registers[128];

  auto mute() const -> bool { return mainvol.mute; }

  u8 channelMask = 0xFF;

  //dsp.cpp
  auto main() -> void;
  auto power(bool reset) -> void;

  //memory.cpp
  auto read(n7 address) -> n8;
  auto write(n7 address, n8 data) -> void;

  // sample output buffer: filled by sample(), read by AresAPU
  s16 sampleLeft = 0;
  s16 sampleRight = 0;
  bool sampleReady = false;

private:
  struct Envelope { enum : u32 {
    Release,
    Attack,
    Decay,
    Sustain,
  };};

  struct Clock {
    n15 counter;
    n1  sample = 1;
  } clock;

  struct MainVol {
    n1  reset = 1;
    n1  mute = 1;
    s8  volume[2];
    i17 output[2];
  } mainvol;

  struct Echo {
    s8  feedback;
    s8  volume[2];
    s8  fir[8];
    i16 history[2][8];
    n8  page;
    n4  delay;
    n1  readonly = 1;
    i17 input[2];
    i17 output[2];

    n8  _page;
    n1  _readonly;
    n16 _address;
    n16 _offset;
    n16 _length;
    n3  _historyOffset;
  } echo;

  struct Noise {
    n5  frequency;
    n15 lfsr = 0x4000;
  } noise;

  struct BRR {
    n8  bank;

    n8  _bank;
    n8  _source;
    n16 _address;
    n16 _nextAddress;
    n8  _header;
    n8  _byte;
  } brr;

  struct Latch {
    n8  adsr0;
    n8  envx;
    n8  outx;
    n15 pitch;
    i16 output;
  } latch;

  struct Voice {
    n7  index;

    s8  volume[2];
    n14 pitch;
    n8  source;
    n8  adsr0;
    n8  adsr1;
    n8  gain;
    n8  envx;
    n1  keyon;
    n1  keyoff;
    n1  modulate;
    n1  noise;
    n1  echo;
    n1  end;

    i16 buffer[12];
    n4  bufferOffset;
    n16 gaussianOffset;
    n16 brrAddress;
    n4  brrOffset = 1;
    n3  keyonDelay;
    n2  envelopeMode;
    n11 envelope;

    s32 _envelope;
    n1  _keylatch;
    n1  _keyon;
    n1  _keyoff;
    n1  _modulate;
    n1  _noise;
    n1  _echo;
    n1  _end;
    n1  _looped;
  } voice[8];

  //gaussian
  i16 gaussianTable[512];
  auto gaussianConstructTable() -> void;
  auto gaussianInterpolate(const Voice& v) -> s32;

  //counter
  static const n16 CounterRate[32];
  static const n16 CounterOffset[32];
  auto counterTick() -> void;
  auto counterPoll(u32 rate) -> bool;

  //envelope
  auto envelopeRun(Voice& v) -> void;

  //brr
  auto brrDecode(Voice& v) -> void;

  //misc
  auto misc27() -> void;
  auto misc28() -> void;
  auto misc29() -> void;
  auto misc30() -> void;

  //voice
  auto voiceOutput(Voice& v, n1 channel) -> void;
  auto voice1 (Voice& v) -> void;
  auto voice2 (Voice& v) -> void;
  auto voice3 (Voice& v) -> void;
  auto voice3a(Voice& v) -> void;
  auto voice3b(Voice& v) -> void;
  auto voice3c(Voice& v) -> void;
  auto voice4 (Voice& v) -> void;
  auto voice5 (Voice& v) -> void;
  auto voice6 (Voice& v) -> void;
  auto voice7 (Voice& v) -> void;
  auto voice8 (Voice& v) -> void;
  auto voice9 (Voice& v) -> void;

  //echo
  auto calculateFIR(n1 channel, s32 index) -> s32;
  auto echoOutput(n1 channel) const -> i16;
  auto echoRead(n1 channel) -> void;
  auto echoWrite(n1 channel) -> void;
  auto echo22() -> void;
  auto echo23() -> void;
  auto echo24() -> void;
  auto echo25() -> void;
  auto echo26() -> void;
  auto echo27() -> void;
  auto echo28() -> void;
  auto echo29() -> void;
  auto echo30() -> void;

  //dsp.cpp
  auto tick() -> void;
  auto sample(i16 left, i16 right) -> void;
};
