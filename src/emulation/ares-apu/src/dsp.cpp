// SNES DSP implementation - adapted from ares/sfc/dsp/
// Original: Copyright (c) 2004-2025 ares team, Near et al (ISC License)

#include "dsp.h"

//=== memory.cpp ===

auto DSP::read(n7 address) -> n8 {
  return registers[address];
}

auto DSP::write(n7 address, n8 data) -> void {
  registers[address] = data;

  switch(address) {
  case 0x0c:  //MVOLL
    mainvol.volume[0] = data;
    break;
  case 0x1c:  //MVOLR
    mainvol.volume[1] = data;
    break;
  case 0x2c:  //EVOLL
    echo.volume[0] = data;
    break;
  case 0x3c:  //EVOLR
    echo.volume[1] = data;
    break;
  case 0x4c:  //KON
    for(u32 n : range(8)) voice[n].keyon = data.bit(n);
    for(u32 n : range(8)) voice[n]._keylatch = data.bit(n);
    break;
  case 0x5c:  //KOFF
    for(u32 n : range(8)) voice[n].keyoff = data.bit(n);
    break;
  case 0x6c:  //FLG
    noise.frequency = data.bit(0,4);
    echo.readonly   = data.bit(5);
    mainvol.mute     = data.bit(6);
    mainvol.reset    = data.bit(7);
    break;
  case 0x7c:  //ENDX
    for(u32 n : range(8)) voice[n]._end = 0;
    registers[0x7c] = 0;
    break;
  case 0x0d:  //EFB
    echo.feedback = data;
    break;
  case 0x2d:  //PMON
    for(u32 n : range(8)) voice[n].modulate = data.bit(n);
    voice[0].modulate = 0;
    break;
  case 0x3d:  //NON
    for(u32 n : range(8)) voice[n].noise = data.bit(n);
    break;
  case 0x4d:  //EON
    for(u32 n : range(8)) voice[n].echo = data.bit(n);
    break;
  case 0x5d:  //DIR
    brr.bank = data;
    break;
  case 0x6d:  //ESA
    echo.page = data;
    break;
  case 0x7d:  //EDL
    echo.delay = data.bit(0,3);
    break;
  }

  n3 n = address.bit(4,6);
  switch((u8)(address & 0x0f)) {
  case 0x00:  //VxVOLL
    voice[n].volume[0] = data;
    break;
  case 0x01:  //VxVOLR
    voice[n].volume[1] = data;
    break;
  case 0x02:  //VxPITCHL
    voice[n].pitch.bit(0,7) = data.bit(0,7);
    break;
  case 0x03:  //VxPITCHH
    voice[n].pitch.bit(8,13) = data.bit(0,5);
    break;
  case 0x04:  //VxSRCN
    voice[n].source = data;
    break;
  case 0x05:  //VxADSR0
    voice[n].adsr0 = data;
    break;
  case 0x06:  //VxADSR1
    voice[n].adsr1 = data;
    break;
  case 0x07:  //VxGAIN
    voice[n].gain = data;
    break;
  case 0x08:  //VxENVX
    latch.envx = data;
    break;
  case 0x09:  //VxOUTX
    latch.outx = data;
    break;
  case 0x0f:  //FIRx
    echo.fir[n] = data;
    break;
  }
}

//=== gaussian.cpp ===

auto DSP::gaussianConstructTable() -> void {
  f64 table[512];
  for(u32 n : range(512)) {
    f64 k = 0.5 + n;
    f64 s = (sin(Math::Pi * k * 1.280 / 1024));
    f64 t = (cos(Math::Pi * k * 2.000 / 1023) - 1) * 0.50;
    f64 u = (cos(Math::Pi * k * 4.000 / 1023) - 1) * 0.08;
    f64 r = s * (t + u + 1.0) / k;
    table[511 - n] = r;
  }
  for(u32 phase : range(128)) {
    f64 sum = 0.0;
    sum += table[phase +   0];
    sum += table[phase + 256];
    sum += table[511 - phase];
    sum += table[255 - phase];
    f64 scale = 2048.0 / sum;
    gaussianTable[phase +   0] = (s16)(table[phase +   0] * scale + 0.5);
    gaussianTable[phase + 256] = (s16)(table[phase + 256] * scale + 0.5);
    gaussianTable[511 - phase] = (s16)(table[511 - phase] * scale + 0.5);
    gaussianTable[255 - phase] = (s16)(table[255 - phase] * scale + 0.5);
  }
}

auto DSP::gaussianInterpolate(const Voice& v) -> s32 {
  n8 offset = v.gaussianOffset >> 4;
  const i16* forward = gaussianTable + 255 - (u64)offset;
  const i16* reverse = gaussianTable       + (u64)offset;

  u32 off = ((u64)v.bufferOffset + (v.gaussianOffset >> 12)) % 12;
  s32 output;
  output  = (s64)forward[  0] * (s64)v.buffer[off] >> 11; if(++off >= 12) off = 0;
  output += (s64)forward[256] * (s64)v.buffer[off] >> 11; if(++off >= 12) off = 0;
  output += (s64)reverse[256] * (s64)v.buffer[off] >> 11; if(++off >= 12) off = 0;
  output  = (s16)output;
  output += (s64)reverse[  0] * (s64)v.buffer[off] >> 11;
  return sclamp<16>(output) & ~1;
}

//=== counter.cpp ===

const n16 DSP::CounterRate[32] = {
     0, 2048, 1536,
  1280, 1024,  768,
   640,  512,  384,
   320,  256,  192,
   160,  128,   96,
    80,   64,   48,
    40,   32,   24,
    20,   16,   12,
    10,    8,    6,
     5,    4,    3,
           2,
           1,
};

const n16 DSP::CounterOffset[32] = {
    0, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
  536, 0, 1040,
       0,
       0,
};

inline auto DSP::counterTick() -> void {
  if(!clock.counter) clock.counter = 2048 * 5 * 3;  //30720 (0x7800)
  clock.counter--;
}

inline auto DSP::counterPoll(u32 rate) -> bool {
  if(rate == 0) return false;
  return ((u64)clock.counter + (u64)CounterOffset[rate]) % (u64)CounterRate[rate] == 0;
}

//=== envelope.cpp ===

auto DSP::envelopeRun(Voice& v) -> void {
  s32 envelope = v.envelope;

  if(v.envelopeMode == Envelope::Release) {
    envelope -= 0x8;
    if(envelope < 0) envelope = 0;
    v.envelope = envelope;
    return;
  }

  s32 rate;
  s32 envelopeData = v.adsr1;
  if(latch.adsr0.bit(7)) {  //ADSR
    if(v.envelopeMode >= Envelope::Decay) {
      envelope--;
      envelope -= envelope >> 8;
      rate = envelopeData & 0x1f;
      if(v.envelopeMode == Envelope::Decay) {
        rate = latch.adsr0.bit(4,6) * 2 + 16;
      }
    } else {  //env_attack
      rate = latch.adsr0.bit(0,3) * 2 + 1;
      envelope += rate < 31 ? 0x20 : 0x400;
    }
  } else {  //GAIN
    envelopeData = v.gain;
    s32 mode = envelopeData >> 5;
    if(mode < 4) {  //direct
      envelope = envelopeData << 4;
      rate = 31;
    } else {
      rate = envelopeData & 0x1f;
      if(mode == 4) {  //linear decrease
        envelope -= 0x20;
      } else if(mode < 6) {  //exponential decrease
        envelope--;
        envelope -= envelope >> 8;
      } else {  //linear increase
        envelope += 0x20;
        if(mode > 6 && (u32)v._envelope >= 0x600) {
          envelope += 0x8 - 0x20;  //two-slope linear increase
        }
      }
    }
  }

  //sustain level
  if((envelope >> 8) == (envelopeData >> 5) && v.envelopeMode == Envelope::Decay) {
    v.envelopeMode = Envelope::Sustain;
  }
  v._envelope = envelope;

  //u32 cast because linear decrease underflowing also triggers this
  if((u32)envelope > 0x7ff) {
    envelope = (envelope < 0 ? 0 : 0x7ff);
    if(v.envelopeMode == Envelope::Attack) v.envelopeMode = Envelope::Decay;
  }

  if(counterPoll(rate)) v.envelope = envelope;
}

//=== brr.cpp ===

auto DSP::brrDecode(Voice& v) -> void {
  s32 nybbles = (u64)brr._byte << 8 | (u64)apuram[n16((u64)v.brrAddress + (u64)v.brrOffset + 1)];

  const s32 filter = brr._header.bit(2,3);
  const s32 scale  = brr._header.bit(4,7);

  for(u32 n : range(4)) {
    s32 s = (s16)nybbles >> 12;
    nybbles <<= 4;

    if(scale <= 12) {
      s <<= scale;
      s >>= 1;
    } else {
      s &= ~0x7ff;
    }

    s32 offset = v.bufferOffset;
    if(--offset < 0) offset = 11; const s32 p1 = v.buffer[offset];
    if(--offset < 0) offset = 11; const s32 p2 = (s64)v.buffer[offset] >> 1;

    switch(filter) {
    case 0:
      break;

    case 1:
      //s += p1 * 0.46875
      s += p1 >> 1;
      s += (-p1) >> 5;
      break;

    case 2:
      //s += p1 * 0.953125 - p2 * 0.46875
      s += p1;
      s -= p2;
      s += p2 >> 4;
      s += (p1 * -3) >> 6;
      break;

    case 3:
      //s += p1 * 0.8984375 - p2 * 0.40625
      s += p1;
      s -= p2;
      s += (p1 * -13) >> 7;
      s += (p2 * 3) >> 4;
      break;
    }

    s = sclamp<16>(s);
    s = (s16)(s << 1);
    v.buffer[v.bufferOffset] = s;
    if(++(v.bufferOffset) >= 12) v.bufferOffset = 0;
  }
}

//=== misc.cpp ===

auto DSP::misc27() -> void {
  for(auto& v : voice) v._modulate = v.modulate;
}

auto DSP::misc28() -> void {
  for(auto& v : voice) v._noise = v.noise, v._echo  = v.echo;
  brr._bank = brr.bank;
}

auto DSP::misc29() -> void {
  clock.sample = !clock.sample;
  if(clock.sample) {
    for(auto& v : voice) v._keylatch &= !v._keyon;
  }
}

auto DSP::misc30() -> void {
  if(clock.sample) {
    for(auto& v : voice) v._keyon = v._keylatch, v._keyoff = v.keyoff;
  }

  counterTick();

  //noise
  if(counterPoll(noise.frequency)) {
    s32 feedback = (u64)noise.lfsr << 13 ^ (u64)noise.lfsr << 14;
    noise.lfsr = feedback & 0x4000 | (u64)noise.lfsr >> 1;
  }
}

//=== voice.cpp ===

inline auto DSP::voiceOutput(Voice& v, n1 channel) -> void {
  s32 amp = (s64)latch.output * v.volume[channel] >> 7;

  if(!(channelMask & (1 << (v.index >> 4)))) amp = 0;

  mainvol.output[channel] += amp;
  mainvol.output[channel] = sclamp<16>((s64)mainvol.output[channel]);

  if(v._echo) {
    echo.output[channel] += amp;
    echo.output[channel] = sclamp<16>((s64)echo.output[channel]);
  }
}

auto DSP::voice1(Voice& v) -> void {
  brr._address = ((u64)brr._bank << 8) + ((u64)brr._source << 2);
  brr._source = v.source;
}

auto DSP::voice2(Voice& v) -> void {
  n16 address = brr._address;
  if(!v.keyonDelay) address += 2;
  brr._nextAddress.byte(0) = (u64)apuram[address++];
  brr._nextAddress.byte(1) = (u64)apuram[address++];
  latch.adsr0 = v.adsr0;

  latch.pitch = v.pitch & 0xff;
}

auto DSP::voice3(Voice& v) -> void {
  voice3a(v);
  voice3b(v);
  voice3c(v);
}

auto DSP::voice3a(Voice& v) -> void {
  latch.pitch |= v.pitch & ~0xff;
}

auto DSP::voice3b(Voice& v) -> void {
  brr._byte   = apuram[n16((u64)v.brrAddress + (u64)v.brrOffset)];
  brr._header = apuram[n16(v.brrAddress)];
}

auto DSP::voice3c(Voice& v) -> void {
  if(v._modulate) {
    latch.pitch += ((s64)latch.output >> 5) * (u64)latch.pitch >> 10;
  }

  if(v.keyonDelay) {
    if(v.keyonDelay == 5) {
      v.brrAddress = brr._nextAddress;
      v.brrOffset = 1;
      v.bufferOffset = 0;
      brr._header = 0;
    }

    v.envelope = 0;
    v._envelope = 0;

    v.gaussianOffset = 0;
    v.keyonDelay--;
    if(v.keyonDelay & 3) v.gaussianOffset = 0x4000;

    latch.pitch = 0;
  }

  s32 output = gaussianInterpolate(v);

  if(v._noise) {
    output = (s16)((u64)noise.lfsr << 1);
  }

  latch.output = output * (u64)v.envelope >> 11 & ~1;
  v.envx = (u64)v.envelope >> 4;

  if(mainvol.reset || brr._header.bit(0,1) == 1) {
    v.envelopeMode = Envelope::Release;
    v.envelope = 0;
  }

  if(clock.sample) {
    if(v._keyoff) {
      v.envelopeMode = Envelope::Release;
    }

    if(v._keyon) {
      v.keyonDelay = 5;
      v.envelopeMode = Envelope::Attack;
    }
  }

  if(!v.keyonDelay) envelopeRun(v);
}

auto DSP::voice4(Voice& v) -> void {
  v._looped = 0;
  if(v.gaussianOffset >= 0x4000) {
    brrDecode(v);
    v.brrOffset += 2;
    if(v.brrOffset >= 9) {
      v.brrAddress = n16((u64)v.brrAddress + 9);
      if(brr._header.bit(0)) {
        v.brrAddress = brr._nextAddress;
        v._looped = 1;
      }
      v.brrOffset = 1;
    }
  }

  v.gaussianOffset = ((u64)v.gaussianOffset & 0x3fff) + (u64)latch.pitch;

  if(v.gaussianOffset > 0x7fff) v.gaussianOffset = 0x7fff;

  voiceOutput(v, 0);
}

auto DSP::voice5(Voice& v) -> void {
  voiceOutput(v, 1);

  v._end |= v._looped;

  if(v.keyonDelay == 5) v._end = 0;
}

auto DSP::voice6(Voice& v) -> void {
  latch.outx = (u64)latch.output >> 8;
}

auto DSP::voice7(Voice& v) -> void {
  {
    u8 endx = 0;
    for(u32 n : range(8)) endx |= (u64)voice[n]._end << n;
    registers[0x7c] = endx;
  }
  latch.envx = v.envx;
}

auto DSP::voice8(Voice& v) -> void {
  registers[(u64)v.index | 0x09] = latch.outx;
}

auto DSP::voice9(Voice& v) -> void {
  registers[(u64)v.index | 0x08] = latch.envx;
}

//=== echo.cpp ===

auto DSP::calculateFIR(n1 channel, s32 index) -> s32 {
  s32 sample = echo.history[channel][n3((u64)echo._historyOffset + index + 1)];
  return (sample * echo.fir[index]) >> 6;
}

auto DSP::echoOutput(n1 channel) const -> i16 {
  i16 mainvolOutput = (s64)mainvol.output[channel] * mainvol.volume[channel] >> 7;
    i16 echoOutput =    (s64)echo.input[channel] *   echo.volume[channel] >> 7;
  return sclamp<16>((s64)mainvolOutput + (s64)echoOutput);
}

auto DSP::echoRead(n1 channel) -> void {
  n16 address = (u64)echo._address + (u64)channel * 2;
  n8 lo = apuram[address++];
  n8 hi = apuram[address++];
  s32 s = (s16)(((u64)hi << 8) + (u64)lo);
  echo.history[channel][(u64)echo._historyOffset] = s >> 1;
}

auto DSP::echoWrite(n1 channel) -> void {
  if(!echo._readonly) {
    n16 address = (u64)echo._address + (u64)channel * 2;
    auto sample = echo.output[channel];
    apuram[address++] = (u64)sample.byte(0);
    apuram[address++] = (u64)sample.byte(1);
  }
  echo.output[channel] = 0;
}

auto DSP::echo22() -> void {
  echo._historyOffset++;

  echo._address = ((u64)echo._page << 8) + (u64)echo._offset;
  echoRead(0);

  s32 l = calculateFIR(0, 0);
  s32 r = calculateFIR(1, 0);

  echo.input[0] = l;
  echo.input[1] = r;
}

auto DSP::echo23() -> void {
  s32 l = calculateFIR(0, 1) + calculateFIR(0, 2);
  s32 r = calculateFIR(1, 1) + calculateFIR(1, 2);

  echo.input[0] += l;
  echo.input[1] += r;

  echoRead(1);
}

auto DSP::echo24() -> void {
  s32 l = calculateFIR(0, 3) + calculateFIR(0, 4) + calculateFIR(0, 5);
  s32 r = calculateFIR(1, 3) + calculateFIR(1, 4) + calculateFIR(1, 5);

  echo.input[0] += l;
  echo.input[1] += r;
}

auto DSP::echo25() -> void {
  s32 l = (s64)echo.input[0] + calculateFIR(0, 6);
  s32 r = (s64)echo.input[1] + calculateFIR(1, 6);

  l = (s16)l;
  r = (s16)r;

  l += (s16)calculateFIR(0, 7);
  r += (s16)calculateFIR(1, 7);

  echo.input[0] = sclamp<16>(l) & ~1;
  echo.input[1] = sclamp<16>(r) & ~1;
}

auto DSP::echo26() -> void {
  mainvol.output[0] = echoOutput(0);

  s32 l = (s64)echo.output[0] + (s16)((s64)echo.input[0] * echo.feedback >> 7);
  s32 r = (s64)echo.output[1] + (s16)((s64)echo.input[1] * echo.feedback >> 7);

  echo.output[0] = sclamp<16>(l) & ~1;
  echo.output[1] = sclamp<16>(r) & ~1;
}

auto DSP::echo27() -> void {
  s32 outl = mainvol.output[0];
  s32 outr = echoOutput(1);
  mainvol.output[0] = 0;
  mainvol.output[1] = 0;

  if(mainvol.mute) {
    outl = 0;
    outr = 0;
  }

  sample(outl, outr);
}

auto DSP::echo28() -> void {
  echo._readonly = echo.readonly;
}

auto DSP::echo29() -> void {
  echo._page = echo.page;

  if(!echo._offset) echo._length = (u64)echo.delay << 11;

  echo._offset += 4;
  if(echo._offset >= echo._length) echo._offset = 0;

  echoWrite(0);

  echo._readonly = echo.readonly;
}

auto DSP::echo30() -> void {
  echoWrite(1);
}

//=== dsp.cpp (main loop, tick, sample, power) ===

auto DSP::main() -> void {
  voice5(voice[0]);
  voice2(voice[1]);
  tick();

  voice6(voice[0]);
  voice3(voice[1]);
  tick();

  voice7(voice[0]);
  voice4(voice[1]);
  voice1(voice[3]);
  tick();

  voice8(voice[0]);
  voice5(voice[1]);
  voice2(voice[2]);
  tick();

  voice9(voice[0]);
  voice6(voice[1]);
  voice3(voice[2]);
  tick();

  voice7(voice[1]);
  voice4(voice[2]);
  voice1(voice[4]);
  tick();

  voice8(voice[1]);
  voice5(voice[2]);
  voice2(voice[3]);
  tick();

  voice9(voice[1]);
  voice6(voice[2]);
  voice3(voice[3]);
  tick();

  voice7(voice[2]);
  voice4(voice[3]);
  voice1(voice[5]);
  tick();

  voice8(voice[2]);
  voice5(voice[3]);
  voice2(voice[4]);
  tick();

  voice9(voice[2]);
  voice6(voice[3]);
  voice3(voice[4]);
  tick();

  voice7(voice[3]);
  voice4(voice[4]);
  voice1(voice[6]);
  tick();

  voice8(voice[3]);
  voice5(voice[4]);
  voice2(voice[5]);
  tick();

  voice9(voice[3]);
  voice6(voice[4]);
  voice3(voice[5]);
  tick();

  voice7(voice[4]);
  voice4(voice[5]);
  voice1(voice[7]);
  tick();

  voice8(voice[4]);
  voice5(voice[5]);
  voice2(voice[6]);
  tick();

  voice9(voice[4]);
  voice6(voice[5]);
  voice3(voice[6]);
  tick();

  voice1(voice[0]);
  voice7(voice[5]);
  voice4(voice[6]);
  tick();

  voice8(voice[5]);
  voice5(voice[6]);
  voice2(voice[7]);
  tick();

  voice9(voice[5]);
  voice6(voice[6]);
  voice3(voice[7]);
  tick();

  voice1(voice[1]);
  voice7(voice[6]);
  voice4(voice[7]);
  tick();

  voice8(voice[6]);
  voice5(voice[7]);
  voice2(voice[0]);
  tick();

  voice3a(voice[0]);
  voice9(voice[6]);
  voice6(voice[7]);
  echo22();
  tick();

  voice7(voice[7]);
  echo23();
  tick();

  voice8(voice[7]);
  echo24();
  tick();

  voice3b(voice[0]);
  voice9(voice[7]);
  echo25();
  tick();

  echo26();
  tick();

  misc27();
  echo27();
  tick();

  misc28();
  echo28();
  tick();

  misc29();
  echo29();
  tick();

  misc30();
  voice3c(voice[0]);
  echo30();
  tick();

  voice4(voice[0]);
  voice1(voice[2]);
  tick();
}

auto DSP::tick() -> void {
  // No-op in standalone mode: cycle accounting handled by AresAPU
}

auto DSP::sample(i16 left, i16 right) -> void {
  sampleLeft = left;
  sampleRight = right;
  sampleReady = true;
}

auto DSP::power(bool reset) -> void {
  if(!reset) {
    memset(apuram, 0, sizeof(apuram));
    memset(registers, 0, sizeof(registers));
  }

  clock = {};
  mainvol = {};
  echo = {};
  noise = {};
  brr = {};
  latch = {};
  for(u32 n : range(8)) {
    voice[n] = {};
    voice[n].index = n << 4;
  }

  gaussianConstructTable();
}
