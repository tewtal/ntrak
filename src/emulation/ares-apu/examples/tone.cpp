// ares-apu tone example
// Loads a minimal SPC700 program that plays a square wave using voice 0.
// Output: raw 16-bit stereo PCM at 32040 Hz to stdout.
// Usage: ./ares-apu-tone | aplay -f S16_LE -r 32040 -c 2
//    or: ./ares-apu-tone > tone.raw

#include "ares-apu.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

int main() {
  AresAPU apu;
  apu.reset(nullptr);

  uint8_t* ram = apu.ram();

  // --- Set up BRR sample data at $2000 ---
  // A simple square wave BRR block (9 bytes per block)
  // BRR header: filter=0, range=12, loop=0, end=0 -> 0xC0
  // Then 8 bytes of nybble data: 0x77 0x77 0x77 0x77 0x88 0x88 0x88 0x88
  // This gives a square wave pattern
  const uint8_t brrBlock[] = {
    0xC2,  // header: range=12, filter=0, end=0, loop=1 (will loop)
    0x77, 0x77, 0x77, 0x77,  // positive nybbles (7 = +7 at range 12)
    0x99, 0x99, 0x99, 0x99,  // negative nybbles (9 = -7 in signed 4-bit)
  };

  // Place the BRR sample data at $2000
  memcpy(ram + 0x2000, brrBlock, 9);

  // Place a second block that loops back (end+loop flags set)
  uint8_t brrBlockLoop[] = {
    0xC3,  // header: range=12, filter=0, end=1, loop=1
    0x77, 0x77, 0x77, 0x77,
    0x99, 0x99, 0x99, 0x99,
  };
  memcpy(ram + 0x2009, brrBlockLoop, 9);

  // --- Set up sample directory at $3000 ---
  // Each entry is 4 bytes: start address (16-bit LE), loop address (16-bit LE)
  // Source 0 entry at directory + 0
  ram[0x3000] = 0x00; ram[0x3001] = 0x20;  // start = $2000
  ram[0x3002] = 0x00; ram[0x3003] = 0x20;  // loop  = $2000

  // --- Configure DSP registers ---
  // DIR = $30 (sample directory at $3000)
  apu.writeDSP(0x5D, 0x30);

  // Voice 0: source = 0
  apu.writeDSP(0x04, 0x00);  // V0SRCN

  // Voice 0: ADSR mode (attack=15, decay=0, sustain level=7, sustain rate=0)
  apu.writeDSP(0x05, 0x8F);  // V0ADSR0: enable ADSR, attack rate=15 (fastest)
  apu.writeDSP(0x06, 0xE0);  // V0ADSR1: sustain level=7 (max), sustain rate=0 (never decrease)

  // Voice 0: pitch = $1000 (base pitch, ~1 kHz)
  apu.writeDSP(0x02, 0x00);  // V0PITCHL
  apu.writeDSP(0x03, 0x10);  // V0PITCHH

  // Voice 0: volume
  apu.writeDSP(0x00, 0x7F);  // V0VOLL = 127
  apu.writeDSP(0x01, 0x7F);  // V0VOLR = 127

  // Main volume
  apu.writeDSP(0x0C, 0x7F);  // MVOLL
  apu.writeDSP(0x1C, 0x7F);  // MVOLR

  // FLG: clear mute and reset
  apu.writeDSP(0x6C, 0x00);  // FLG: mute=0, reset=0, echo readonly=0

  // Echo off
  apu.writeDSP(0x4D, 0x00);  // EON = 0
  apu.writeDSP(0x6D, 0x00);  // ESA = 0
  apu.writeDSP(0x7D, 0x00);  // EDL = 0

  // KON: key on voice 0
  apu.writeDSP(0x4C, 0x01);

  // --- Write a simple SPC700 program at $0200 that just loops ---
  // The SMP will execute this program, but since we've configured the DSP
  // directly, it just needs to idle and let the DSP run.
  // $0200: BRA $0200  (infinite loop: 2F FE)
  ram[0x0200] = 0x2F;  // BRA
  ram[0x0201] = 0xFE;  // offset -2 (loop to self)

  // Generate ~3 seconds of audio (32040 samples/sec * 3 = 96120 samples)
  for(int i = 0; i < 32040 * 3; i++) {
    auto sample = apu.step();
    // Write as little-endian 16-bit stereo
    uint8_t buf[4];
    buf[0] = sample.left & 0xFF;
    buf[1] = (sample.left >> 8) & 0xFF;
    buf[2] = sample.right & 0xFF;
    buf[3] = (sample.right >> 8) & 0xFF;
    fwrite(buf, 1, 4, stdout);
  }

  return 0;
}
